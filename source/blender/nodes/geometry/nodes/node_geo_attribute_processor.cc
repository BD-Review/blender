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

#include "BLI_kdopbvh.h"
#include "BLI_kdtree.h"
#include "BLI_task.hh"
#include "BLI_timeit.hh"

#include "DNA_mesh_types.h"

#include "BKE_bvhutils.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "node_geometry_util.hh"

static void geo_node_attribute_processor_layout(uiLayout *UNUSED(layout),
                                                bContext *UNUSED(C),
                                                PointerRNA *UNUSED(ptr))
{
}

static void geo_node_attribute_processor_init(bNodeTree *UNUSED(ntree), bNode *node)
{
  NodeGeometryAttributeProcessor *node_storage = (NodeGeometryAttributeProcessor *)MEM_callocN(
      sizeof(NodeGeometryAttributeProcessor), __func__);

  node->storage = node_storage;
}

namespace blender::nodes {

static void geo_node_attribute_processor_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");
  GeometrySet geometry_set_target = params.extract_input<GeometrySet>("Target");

  geometry_set = geometry_set_realize_instances(geometry_set);

  params.set_output("Geometry", geometry_set);
}

}  // namespace blender::nodes

void register_node_type_geo_attribute_processor()
{
  static bNodeType ntype;

  geo_node_type_base(
      &ntype, GEO_NODE_ATTRIBUTE_PROCESSOR, "Attribute Processor", NODE_CLASS_ATTRIBUTE, 0);
  node_type_init(&ntype, geo_node_attribute_processor_init);
  node_type_storage(&ntype,
                    "NodeGeometryAttributeProcessor",
                    node_free_standard_storage,
                    node_copy_standard_storage);
  ntype.geometry_node_execute = blender::nodes::geo_node_attribute_processor_exec;
  ntype.draw_buttons = geo_node_attribute_processor_layout;
  nodeRegisterType(&ntype);
}
