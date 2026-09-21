#include "GodotTransformCodec.h"

#include <idtxflow/net/model/ConventionMath.h>

using namespace godot;

namespace idtxflow
{
namespace collab_godot
{
namespace xform
{
    void transform_to_prim_edit(const std::string& prim_path, const Transform3D& t,
                                net::model::PrimEdit& out)
    {
        out.kind = net::model::PrimEdit::Kind::Transform;
        out.prim_path = prim_path;
        out.is_matrix = true;
        const Basis& b = t.basis;
        const double basis_rows[9] = {
            b.rows[0][0], b.rows[0][1], b.rows[0][2],
            b.rows[1][0], b.rows[1][1], b.rows[1][2],
            b.rows[2][0], b.rows[2][1], b.rows[2][2],
        };
        const double origin[3] = {t.origin.x, t.origin.y, t.origin.z};
        out.matrix = net::model::wire_from_basis_origin(basis_rows, origin);
    }

    net::model::PrimEdit transform_to_prim_edit(const std::string& prim_path, const Transform3D& t)
    {
        net::model::PrimEdit e;
        transform_to_prim_edit(prim_path, t, e);
        return e;
    }

    Transform3D mat4_to_transform(const net::model::Mat4& mm)
    {
        double basis_rows[9];
        double origin[3];
        net::model::basis_origin_from_wire(mm, basis_rows, origin);
        Basis basis;
        basis.rows[0] = Vector3((real_t)basis_rows[0], (real_t)basis_rows[1], (real_t)basis_rows[2]);
        basis.rows[1] = Vector3((real_t)basis_rows[3], (real_t)basis_rows[4], (real_t)basis_rows[5]);
        basis.rows[2] = Vector3((real_t)basis_rows[6], (real_t)basis_rows[7], (real_t)basis_rows[8]);
        return Transform3D(basis, Vector3((real_t)origin[0], (real_t)origin[1], (real_t)origin[2]));
    }

    Transform3D separate_to_transform(const net::model::SeparateXform& s)
    {
        Transform3D t;
        t.origin = Vector3((real_t)s.translation[0], (real_t)s.translation[1], (real_t)s.translation[2]);
        Basis b = Basis::from_euler(Vector3(
            Math::deg_to_rad((real_t)s.rotation[0]),
            Math::deg_to_rad((real_t)s.rotation[1]),
            Math::deg_to_rad((real_t)s.rotation[2])));
        b.scale(Vector3((real_t)s.scale[0], (real_t)s.scale[1], (real_t)s.scale[2]));
        t.basis = b;
        return t;
    }

    Transform3D prim_edit_to_transform(const net::model::PrimEdit& e)
    {
        return e.is_matrix ? mat4_to_transform(e.matrix) : separate_to_transform(e.separate);
    }

} // namespace xform
} // namespace collab_godot
} // namespace idtxflow
