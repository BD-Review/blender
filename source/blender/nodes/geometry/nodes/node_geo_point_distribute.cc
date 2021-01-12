/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "BLI_float3.hh"
#include "BLI_hash.h"
#include "BLI_kdtree.h"
#include "BLI_math_vector.h"
#include "BLI_rand.hh"
#include "BLI_span.hh"
#include "BLI_timeit.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_pointcloud_types.h"

#include "BKE_bvhutils.h"
#include "BKE_deform.h"
#include "BKE_mesh.h"
#include "BKE_mesh_runtime.h"
#include "BKE_pointcloud.h"

#include "node_geometry_util.hh"

static bNodeSocketTemplate geo_node_point_distribute_in[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {SOCK_FLOAT, N_("Distance Min"), 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 100000.0f, PROP_NONE},
    {SOCK_FLOAT, N_("Density Max"), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100000.0f, PROP_NONE},
    {SOCK_STRING, N_("Density Attribute")},
    {SOCK_INT, N_("Seed"), 0, 0, 0, 0, -10000, 10000},
    {-1, ""},
};

static bNodeSocketTemplate geo_node_point_distribute_out[] = {
    {SOCK_GEOMETRY, N_("Geometry")},
    {-1, ""},
};

static void node_point_distribute_update(bNodeTree *UNUSED(ntree), bNode *node)
{
  bNodeSocket *sock_min_dist = (bNodeSocket *)BLI_findlink(&node->inputs, 1);

  nodeSetSocketAvailability(sock_min_dist, ELEM(node->custom1, GEO_NODE_POINT_DISTRIBUTE_POISSON));
}

namespace blender::nodes {

/**
 * Use an arbitrary choice of axes for a usable rotation attribute directly out of this node.
 */
static float3 normal_to_euler_rotation(const float3 normal)
{
  float quat[4];
  vec_to_quat(quat, normal, OB_NEGZ, OB_POSY);
  float3 rotation;
  quat_to_eul(rotation, quat);
  return rotation;
}

static Span<MLoopTri> get_mesh_looptris(const Mesh &mesh)
{
  /* This only updates a cache and can be considered to be logically const. */
  const MLoopTri *looptris = BKE_mesh_runtime_looptri_ensure(const_cast<Mesh *>(&mesh));
  const int looptris_len = BKE_mesh_runtime_looptri_len(&mesh);
  return {looptris, looptris_len};
}

static Vector<float3> random_scatter_points_from_mesh(const Mesh *mesh,
                                                      const float density,
                                                      const FloatReadAttribute &density_factors,
                                                      Vector<float3> &r_normals,
                                                      Vector<int> &r_ids,
                                                      const int seed)
{
  Span<MLoopTri> looptris = get_mesh_looptris(*mesh);

  Vector<float3> points;

  for (const int looptri_index : looptris.index_range()) {
    const MLoopTri &looptri = looptris[looptri_index];
    const int v0_index = mesh->mloop[looptri.tri[0]].v;
    const int v1_index = mesh->mloop[looptri.tri[1]].v;
    const int v2_index = mesh->mloop[looptri.tri[2]].v;
    const float3 v0_pos = mesh->mvert[v0_index].co;
    const float3 v1_pos = mesh->mvert[v1_index].co;
    const float3 v2_pos = mesh->mvert[v2_index].co;
    const float v0_density_factor = std::max(0.0f, density_factors[v0_index]);
    const float v1_density_factor = std::max(0.0f, density_factors[v1_index]);
    const float v2_density_factor = std::max(0.0f, density_factors[v2_index]);
    const float looptri_density_factor = (v0_density_factor + v1_density_factor +
                                          v2_density_factor) /
                                         3.0f;
    const float area = area_tri_v3(v0_pos, v1_pos, v2_pos);

    const int looptri_seed = BLI_hash_int(looptri_index + seed);
    RandomNumberGenerator looptri_rng(looptri_seed);

    const float points_amount_fl = area * density * looptri_density_factor;
    const float add_point_probability = fractf(points_amount_fl);
    const bool add_point = add_point_probability > looptri_rng.get_float();
    const int point_amount = (int)points_amount_fl + (int)add_point;

    for (int i = 0; i < point_amount; i++) {
      const float3 bary_coords = looptri_rng.get_barycentric_coordinates();
      float3 point_pos;
      interp_v3_v3v3v3(point_pos, v0_pos, v1_pos, v2_pos, bary_coords);
      points.append(point_pos);

      /* Build a hash stable even when the mesh is deformed. */
      r_ids.append((int)(bary_coords.hash()) + looptri_index);

      float3 tri_normal;
      normal_tri_v3(tri_normal, v0_pos, v1_pos, v2_pos);
      r_normals.append(tri_normal);
    }
  }

  return points;
}

BLI_NOINLINE static void initial_uniform_distribution(const Mesh &mesh,
                                                      const float density,
                                                      const int seed,
                                                      Vector<float3> &r_positions,
                                                      Vector<float3> &r_bary_coords,
                                                      Vector<int> &r_looptri_indices)
{
  Span<MLoopTri> looptris = get_mesh_looptris(mesh);

  for (const int looptri_index : looptris.index_range()) {
    const MLoopTri &looptri = looptris[looptri_index];
    const int v0_index = mesh.mloop[looptri.tri[0]].v;
    const int v1_index = mesh.mloop[looptri.tri[1]].v;
    const int v2_index = mesh.mloop[looptri.tri[2]].v;
    const float3 v0_pos = mesh.mvert[v0_index].co;
    const float3 v1_pos = mesh.mvert[v1_index].co;
    const float3 v2_pos = mesh.mvert[v2_index].co;
    const float area = area_tri_v3(v0_pos, v1_pos, v2_pos);
    const int looptri_seed = BLI_hash_int(looptri_index + seed);
    RandomNumberGenerator looptri_rng(looptri_seed);

    const float points_amount_fl = area * density;
    const float add_point_probability = fractf(points_amount_fl);
    const bool add_point = add_point_probability > looptri_rng.get_float();
    const int point_amount = (int)points_amount_fl + (int)add_point;

    for (int i = 0; i < point_amount; i++) {
      const float3 bary_coords = looptri_rng.get_barycentric_coordinates();
      float3 point_pos;
      interp_v3_v3v3v3(point_pos, v0_pos, v1_pos, v2_pos, bary_coords);
      r_positions.append(point_pos);
      r_bary_coords.append(bary_coords);
      r_looptri_indices.append(looptri_index);
    }
  }
}

BLI_NOINLINE static KDTree_3d *build_kdtree(Span<float3> positions)
{
  KDTree_3d *kdtree = BLI_kdtree_3d_new(positions.size());
  for (const int i : positions.index_range()) {
    BLI_kdtree_3d_insert(kdtree, i, positions[i]);
  }
  BLI_kdtree_3d_balance(kdtree);
  return kdtree;
}

BLI_NOINLINE static void create_elimination_mask_for_close_points(
    Span<float3> positions, const float minimum_distance, MutableSpan<bool> r_elimination_mask)
{
  KDTree_3d *kdtree = build_kdtree(positions);

  for (const int i : positions.index_range()) {
    if (r_elimination_mask[i]) {
      continue;
    }

    struct CallbackData {
      int index;
      MutableSpan<bool> elimination_mask;
    } callback_data = {i, r_elimination_mask};

    BLI_kdtree_3d_range_search_cb(
        kdtree,
        positions[i],
        minimum_distance,
        [](void *user_data, int index, const float *UNUSED(co), float UNUSED(dist_sq)) {
          CallbackData &callback_data = *static_cast<CallbackData *>(user_data);
          if (index != callback_data.index) {
            callback_data.elimination_mask[index] = true;
          }
          return true;
        },
        &callback_data);
  }
  BLI_kdtree_3d_free(kdtree);
}

BLI_NOINLINE static void eliminate_points_based_on_mask(Span<bool> elimination_mask,
                                                        Vector<float3> &positions,
                                                        Vector<float3> &bary_coords,
                                                        Vector<int> &looptri_indices)
{
  for (int i = positions.size() - 1; i >= 0; i--) {
    if (elimination_mask[i]) {
      positions.remove_and_reorder(i);
      bary_coords.remove_and_reorder(i);
      looptri_indices.remove_and_reorder(i);
    }
  }
}

BLI_NOINLINE static void compute_remaining_point_data(const Mesh &mesh,
                                                      Span<float3> bary_coords,
                                                      Span<int> looptri_indices,
                                                      MutableSpan<float3> r_normals,
                                                      MutableSpan<int> r_ids)
{
  Span<MLoopTri> looptris = get_mesh_looptris(mesh);
  for (const int i : bary_coords.index_range()) {
    const int looptri_index = looptri_indices[i];
    const MLoopTri &looptri = looptris[looptri_index];
    const float3 &bary_coord = bary_coords[i];

    const int v0_index = mesh.mloop[looptri.tri[0]].v;
    const int v1_index = mesh.mloop[looptri.tri[1]].v;
    const int v2_index = mesh.mloop[looptri.tri[2]].v;
    const float3 v0_pos = mesh.mvert[v0_index].co;
    const float3 v1_pos = mesh.mvert[v1_index].co;
    const float3 v2_pos = mesh.mvert[v2_index].co;

    r_ids[i] = (int)(bary_coord.hash()) + looptri_index;

    normal_tri_v3(r_normals[i], v0_pos, v1_pos, v2_pos);
  }
}

static Vector<float3> stable_random_scatter_with_minimum_distance(
    const Mesh &mesh,
    const float max_density,
    const float minimum_distance,
    const FloatReadAttribute &density_factors,
    Vector<float3> &r_normals,
    Vector<int> &r_ids,
    const int seed)
{
  SCOPED_TIMER(__func__);

  Vector<float3> positions;
  Vector<float3> bary_coords;
  Vector<int> looptri_indices;
  initial_uniform_distribution(mesh, max_density, seed, positions, bary_coords, looptri_indices);
  Array<bool> elimination_mask(positions.size(), false);
  create_elimination_mask_for_close_points(positions, minimum_distance, elimination_mask);
  eliminate_points_based_on_mask(elimination_mask, positions, bary_coords, looptri_indices);

  const int tot_output_points = positions.size();
  r_normals.resize(tot_output_points);
  r_ids.resize(tot_output_points);
  compute_remaining_point_data(mesh, bary_coords, looptri_indices, r_normals, r_ids);
  return positions;
}

static void geo_node_point_distribute_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");
  GeometrySet geometry_set_out;

  GeometryNodePointDistributeMethod distribute_method =
      static_cast<GeometryNodePointDistributeMethod>(params.node().custom1);

  if (!geometry_set.has_mesh()) {
    params.set_output("Geometry", std::move(geometry_set_out));
    return;
  }

  const float density = params.extract_input<float>("Density Max");
  const std::string density_attribute = params.extract_input<std::string>("Density Attribute");

  if (density <= 0.0f) {
    params.set_output("Geometry", std::move(geometry_set_out));
    return;
  }

  const MeshComponent &mesh_component = *geometry_set.get_component_for_read<MeshComponent>();
  const Mesh *mesh_in = mesh_component.get_for_read();

  if (mesh_in == nullptr || mesh_in->mpoly == nullptr) {
    params.set_output("Geometry", std::move(geometry_set_out));
    return;
  }

  const FloatReadAttribute density_factors = mesh_component.attribute_get_for_read<float>(
      density_attribute, ATTR_DOMAIN_POINT, 1.0f);
  const int seed = params.get_input<int>("Seed");

  Vector<int> stable_ids;
  Vector<float3> normals;
  Vector<float3> points;
  switch (distribute_method) {
    case GEO_NODE_POINT_DISTRIBUTE_RANDOM:
      points = random_scatter_points_from_mesh(
          mesh_in, density, density_factors, normals, stable_ids, seed);
      break;
    case GEO_NODE_POINT_DISTRIBUTE_POISSON:
      const float minimum_distance = params.extract_input<float>("Distance Min");
      points = stable_random_scatter_with_minimum_distance(
          *mesh_in, density, minimum_distance, density_factors, normals, stable_ids, seed);
      break;
  }

  PointCloud *pointcloud = BKE_pointcloud_new_nomain(points.size());
  memcpy(pointcloud->co, points.data(), sizeof(float3) * points.size());
  for (const int i : points.index_range()) {
    *(float3 *)(pointcloud->co + i) = points[i];
    pointcloud->radius[i] = 0.05f;
  }

  PointCloudComponent &point_component =
      geometry_set_out.get_component_for_write<PointCloudComponent>();
  point_component.replace(pointcloud);

  {
    Int32WriteAttribute stable_id_attribute = point_component.attribute_try_ensure_for_write(
        "id", ATTR_DOMAIN_POINT, CD_PROP_INT32);
    MutableSpan<int> stable_ids_span = stable_id_attribute.get_span();
    stable_ids_span.copy_from(stable_ids);
    stable_id_attribute.apply_span();
  }

  {
    Float3WriteAttribute normals_attribute = point_component.attribute_try_ensure_for_write(
        "normal", ATTR_DOMAIN_POINT, CD_PROP_FLOAT3);
    MutableSpan<float3> normals_span = normals_attribute.get_span();
    normals_span.copy_from(normals);
    normals_attribute.apply_span();
  }

  {
    Float3WriteAttribute rotations_attribute = point_component.attribute_try_ensure_for_write(
        "rotation", ATTR_DOMAIN_POINT, CD_PROP_FLOAT3);
    MutableSpan<float3> rotations_span = rotations_attribute.get_span();
    for (const int i : rotations_span.index_range()) {
      rotations_span[i] = normal_to_euler_rotation(normals[i]);
    }
    rotations_attribute.apply_span();
  }

  params.set_output("Geometry", std::move(geometry_set_out));
}
}  // namespace blender::nodes

void register_node_type_geo_point_distribute()
{
  static bNodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_POINT_DISTRIBUTE, "Point Distribute", NODE_CLASS_GEOMETRY, 0);
  node_type_socket_templates(&ntype, geo_node_point_distribute_in, geo_node_point_distribute_out);
  node_type_update(&ntype, node_point_distribute_update);
  ntype.geometry_node_execute = blender::nodes::geo_node_point_distribute_exec;
  nodeRegisterType(&ntype);
}
