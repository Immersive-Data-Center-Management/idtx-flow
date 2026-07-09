#include "UsdPathNode3D.h"



using namespace godot;

void UsdPathNode3D::_enter_tree()
{
    Node3D::_enter_tree();
    
    set_position(transform_data_.get_origin());
}

void UsdPathNode3D::convert_points(
    godot::Ref<godot::Curve3D>& curve,
    int& vertexCount,
    godot::PackedVector3Array& points, 
    bool closed)
{
    // last point index
    int end_idx = vertexCount-1;
    
    // open path
    if (closed == false)
    {
        // take care of 'body' segments
        if (vertexCount > 4)
        {
            for (int i=3; i <= vertexCount-4; i+=3)
            {
                // in & out handle positions are relative to the point position
                // need to pre-calculate then
                Vector3 in = points[i-1] - points[i];
                Vector3 out = points[i+1] - points[i];
                
                curve->add_point(
                    points[i],  // point
                    in,         // inHandle
                    out,        // outHandle
                    i);         // index
            }
        }
        
        // p0 only has point + outHandle
        Vector3 out = points[1] - points[0];
        curve->add_point(
                points[0],                                      // point
                godot::Vector3(0.0f, 0.0f, 0.0f),   // inHandle
                out,                                            // outHandle
                0);                                     // index
        
        // end point only has point + inHandle
        Vector3 in = points[end_idx-1] - points[end_idx];
        
        curve->add_point(
                points[end_idx],                                // point
                in,                                             // inHandle
                godot::Vector3(0.0f, 0.0f, 0.0f),   // outHandle
                end_idx);                                       // index
    }
    // closed path
    else
    {
        // we need at least 4 points for conversion (2x points and 2x handles)
        if (vertexCount > 4)
        {
            for (int i=0; i <= vertexCount-1; i+=3)
            {
                // wrap i around (0 - vertexCount-1)
                int in_idx = (i - 1 + vertexCount) % vertexCount;
                int out_idx = (i + 1) % vertexCount;
                
                // in & out handle positions are relative to the point position
                Vector3 in = points[in_idx] - points[i];
                Vector3 out = points[out_idx] - points[i];
                
                curve->add_point(
                    points[i],  // point
                    in,         // inHandle
                    out,        // outHandle
                    i);         // index
            }
        }
    }
    
}

void UsdPathNode3D::_bind_methods()
{
    IUSDNODE_IMPLEMENT_BINDINGS(UsdPathNode3D)
    
    
}
