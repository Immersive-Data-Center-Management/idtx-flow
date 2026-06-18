#pragma once

/**
 * @file MeshConverter.h
 * @brief Converter for UsdGeomMesh prims into a generic MeshDescription format that can be further processed into
 * engine-specific mesh assets.
 * 
 * The conversion extracts vertex positions, face indices, normals, UVs, vertex colors and material assignments.
 * It does not support more complex features like blend shapes or skeletal animation.
 * 
 * Skeletal Prims are handled separately in the UsdSkeletalMeshConverter, which extracts the skinning data and skeleton bindings.
 * The actual mesh data is extracted in this converter and can be used by the skeletal mesh converter to create the final skeletal mesh asset.
 */
#include <optional>
#include <vector>

#include <pxr/base/tf/token.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/skinningQuery.h>

#include "../types/TargetTypes.h"
#include "TypeConverter.h"

namespace idtxflow
{
namespace converter
{
	template<typename MeshDataType>
	struct MeshDescription
	{
		MeshDataType meshData;
		pxr::UsdShadeMaterial usdMaterial;
	};
	
	template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
	class UsdMeshConverter
	{
	public:
		using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
		using TypeConverter = UsdTypeConverter<TargetEngine>;
		using MeshBuilder = TargetMeshBuilder<TargetEngine>;
		using MeshDataType = typename Types::MeshData;

		/**
		 * General core conversation of an UsdGeomMesh prim into the data structures required by the
		 * respective target engine, the usd data should be converted into. The resulting array consists of
		 * self-contained complete mesh definitions with its points, normal vectors, uv and vertex colors if authored.
		 * It's the responsibility of the calling code, implementetd for the specific engine, using the returned data
		 * and adds it to the respective objects, like StaticMeshComponents or similar.
		 * @param usdMesh The reference to the usd prim.
		 * @return Array of self-contained mesh data container
		 */
		std::vector<MeshDescription<MeshDataType>> Convert(const pxr::UsdGeomMesh& usdMesh)
		{
			if (!usdMesh.GetPointsAttr().Get(&points))
				return {}; // a mesh w/o points does not make any sense
			if (!usdMesh.GetFaceVertexCountsAttr().Get(&facePointCounts))
				return {}; // a mesh w/o faces does not make any sense
			if (!usdMesh.GetFaceVertexIndicesAttr().Get(&facePointIndices))
				return {}; // a mesh w/o face-vertex-index assignment does not make any sense

			// calculate the offsets into the facePointIndices list from the facePointCounts list
			// TODO: we might be able to skip this, if we can add some custom metadata to the mesh
			// if it has been pre-optimized to always contain faces with the same amount of points
			int offset = 0;
			facePointOffsets.reserve(facePointCounts.size());
			for (int& faceCount: facePointCounts)
			{
				facePointOffsets.push_back(offset);
				offset += faceCount;
			}

			if (!usdMesh.GetOrientationAttr().Get(&orientation))
				orientation = pxr::UsdGeomTokens->leftHanded; // default the face orientation to leftHanded
			
			usdMesh.GetDisplayColorAttr().Get(&colors);
			colorInterpolation = usdMesh.GetDisplayColorPrimvar().GetInterpolation();

			usdMesh.GetNormalsAttr().Get(&normals);
			normalInterpolation = usdMesh.GetNormalsInterpolation();

			// extracting the texture coordinates (UV-Map) for a mesh from USD could be a bit tricky.
			// the texture coordinate list is authored as PrimVar to the mesh. However, even though the
			// name is quite standard, the actual attribute used is quite often authored in the assigned shader/material
			// with a specific variable in the "inputs" namespace. Thus we would require to lookup the assigned shader here.
			// To reduce this complexity we stick to the common naming convention to have the UV's authored as "st" or "st0".
			if (const pxr::UsdGeomPrimvarsAPI& primVars = pxr::UsdGeomPrimvarsAPI(usdMesh.GetPrim()))
			{
				pxr::UsdGeomPrimvar texcoords_var;
				if (primVars.HasPrimvar(pxr::TfToken("st")))
					texcoords_var = primVars.GetPrimvar(pxr::TfToken("st"));
				else if (primVars.HasPrimvar(pxr::TfToken("st0")))
					texcoords_var = primVars.GetPrimvar(pxr::TfToken("st0"));

				if (texcoords_var)
				{
					texcoords_var.Get(&texcoords);
					texcoords_var.GetIndices(&texcoordIndices);
					texcoordsInterpolation = texcoords_var.GetInterpolation();
				}
			}

			// once we have extracted all relevant data from the UsdGeomMeshPrim we can run the conversion into the
			// target engine format
			std::vector<MeshDescription<MeshDataType>> engineMesh;

			// as the USD mesh may contain subsets we need to create the subset meshes first and finish of with the remainder.
			// while the USD subsets does only authore an index list of faces/edges/points into that refer to the original mesh
			// we need to create self-containing complete meshes for the target engine containing all vertices and face indicies.
			// we will store all subset faces that has been considered already. Only those faces that have not been considered at all
			// will then be converted in a final step. If a mesh does not contain any subsets, this will lead to converting the
			// whole mesh in the final step, as expected.
			pxr::VtArray<int> handledSubsetFaces;
			for (const pxr::UsdGeomSubset& subset : pxr::UsdGeomSubset::GetAllGeomSubsets(usdMesh))
			{
				class pxr::TfToken subsetType;
				// if the subset does not define it's type (face, edge, point) we can't handle it
				if (!subset.GetElementTypeAttr().Get(&subsetType)) continue;

				// for the time beeing we only handle subsets of type "Face".
				if (subsetType != pxr::UsdGeomTokens->face) continue;

				pxr::VtArray<int> subsetFaces;
				// if the subset does not define any faces we can't handle it
				if (!subset.GetIndicesAttr().Get(&subsetFaces)) continue;

				// now build the mesh data for this subset
				MeshDescription<MeshDataType> subsetData;
				subsetData.meshData = BuildMesh(subsetFaces);

				// store the handled faces in the respective list
				pxr::VtArray<int> tmpSubsetFaces;
				tmpSubsetFaces.resize(handledSubsetFaces.size() + subsetFaces.size());
				std::copy(handledSubsetFaces.begin(), handledSubsetFaces.end(), tmpSubsetFaces.begin());
				std::copy(subsetFaces.begin(), subsetFaces.end(), tmpSubsetFaces.begin() + handledSubsetFaces.size());
				handledSubsetFaces = tmpSubsetFaces;
				
				// TODO: get the material assigned to this subset if any and either convert here, or
				// let the caller do the convertion and further processing of the material and just
				// provide the material reference
				subsetData.usdMaterial = GetUsdMaterial(subset);

				// push the subset mesh to the mesh data list. It's up to the engine to process the different meshes and create
				// the respective data in the engine format
				engineMesh.push_back(subsetData);
			}

			// after handling all subsets - if any - handle the remainder of the mesh
			pxr::VtArray<int> remainingFaces;
			if (!handledSubsetFaces.empty() && handledSubsetFaces.size() < facePointCounts.size())
			{
				// calculate the remaining faces
				remainingFaces.reserve(facePointCounts.size() - handledSubsetFaces.size());
				for (int f = 0; f < facePointCounts.size(); ++f)
				{
					// if the face is not found in the subset faces, we need to consider it for conversion
					if (std::find(handledSubsetFaces.begin(), handledSubsetFaces.end(), f) == handledSubsetFaces.end())
						remainingFaces.push_back(f);
				}
			}

			// if either remaining faces exists, or no subset has been handled, convert the remaining faces of
			// the mesh. if remainingFaces is empty, the whole mesh is converted.
			if (!remainingFaces.empty() || handledSubsetFaces.empty())
			{
				MeshDescription<MeshDataType> meshData;
				meshData.meshData = BuildMesh(remainingFaces);

				// TODO: get the material assigned to this subset if any and either convert here, or
				// let the caller do the convertion and further processing of the material and just
				// provide the material reference
				meshData.usdMaterial = GetUsdMaterial(usdMesh);

				// push the mesh data to the list. It's up to the engine to process the different meshes and create
				// the respective data in the engine format
				engineMesh.push_back(meshData);
			}

			return engineMesh;
		}

	    std::vector<MeshDescription<MeshDataType>> ConvertSkinnedMesh(
	        const pxr::UsdSkelSkinningQuery& skinningQuery,
	        const pxr::UsdSkelSkeletonQuery& skelQuery,
	        const std::map<std::string, int32_t>& boneNameToIndexMap)
		{
		    pxr::UsdGeomMesh usdMesh(skinningQuery.GetPrim());
		    if (!usdMesh) return {};

		    skinningQuery.GetJointOrder(&joint_order);
		    skinningQuery.GetJointIndicesPrimvar().Get(&joint_indices);
		    joint_index_element_size = skinningQuery.GetJointIndicesPrimvar().GetElementSize();
		    skinningQuery.GetJointWeightsPrimvar().Get(&joint_weights);
		    joint_weights_element_size = skinningQuery.GetJointWeightsPrimvar().GetElementSize();

		    boneNameToIndex = boneNameToIndexMap;

		    return Convert(usdMesh);
		}

	    pxr::UsdShadeMaterial GetUsdMaterial(const pxr::UsdSchemaBase& schemaObj)
		{
			pxr::UsdShadeMaterialBindingAPI material_binding(schemaObj);
		    // MaterialBindingAPI is a "builtIn" schema and should always be available for Geom prims. But to be save
		    // we fall back to a default material in this case
		    if (!material_binding) return pxr::UsdShadeMaterial();
		    
		    pxr::UsdRelationship binding_relationship;
		    pxr::UsdShadeMaterial material = material_binding.ComputeBoundMaterial(pxr::UsdShadeTokens->allPurpose, &binding_relationship);
		    if (material) return material;
		    // if the BindingRelation ship is invalid it means, that no material binding has been authored. This is ok and will
		    // lead to a default material beeing used.
		    if (!binding_relationship.IsValid()) return pxr::UsdShadeMaterial();
		    
		    // not being able to get the material binding could be, that the bound material is part of another usd layer that is
		    // referenced via payload and the payload is not yet loaded. Check for this case. As this may be the case along the
		    // path to the actual material on any parent level we walk the path up.
		    // even though there could be technically multiple targets maintained for the material binding relationship
		    // we will only consider the first one.
			pxr::UsdStageRefPtr stage = schemaObj.GetPrim().GetStage();
		    pxr::SdfPathVector targets;
		    binding_relationship.GetTargets(&targets);
		    for (const pxr::SdfPath& target : targets)
		    {
		        pxr::UsdPrim material_prim = stage->GetPrimAtPath(target);
		        while (!material_prim)
		        {
		            pxr::SdfPath parent = target.GetParentPath();
		            if (parent.IsEmpty() || parent.IsAbsoluteRootPath()) break;
		            material_prim = stage->GetPrimAtPath(parent);
		        }

		        if (material_prim)
		        {
		            // now ensure the prim is loaded, this will also load any descendant prims. We assume, this load is not heavy
		            // and will occur for the "materials in payload usd layers" use case
		            material_prim.Load();
		            material = material_binding.ComputeBoundMaterial(pxr::UsdShadeTokens->allPurpose);
		            // if this still would not provide a material, we will fallback to a default one, which is fine then
		            // for the time being.
		            return material;
		        }

		        break;
		    }
			
		    return pxr::UsdShadeMaterial();
		}

	protected:
		// build the target engines mesh data. If the usdMesh has been split into subsets, the faceFilter provides the
		// faces of the subset that shall be considered for mesh creation.
		MeshDataType BuildMesh(const pxr::VtArray<int>& faceFilter)
		{
			MeshDataType meshData;
			MeshBuilder builder;
			
			// run the conversion face by face. Either all faces of the mesh or only the ones provided in the filer
			int faces = !faceFilter.empty() ? faceFilter.size() : facePointCounts.size();
			for (int f = 0; f < faces; ++f)
			{
				// get the face index
				int faceIndex = !faceFilter.empty() ? faceFilter[f] : f;
				// get the point count of this face and the offset into the point index list
				int pointCount = facePointCounts[faceIndex];
				int facePointOffset = facePointOffsets[faceIndex];

				if (pointCount < 3) continue; // we will not convert degenerate faces

				// while USD most likely stores mesh data in an optimized format and re-uses vertex data as much as
				// as possible, game engine usually require all data (point, normal, uv, color) to be stored in arrays
				// of the same size and thus for each individual point/vertex. Thus sharing data of a vertex for
				// multiple faces would only be possible if all faces, using the same point would also have the same
				// uv, normal and color authored for all faces sharing this point.
				// To reduce complexity in the initial conversion version we assume that each point has to have it's unique
				// data and thus each point and it's related data is copied for each face using this point. It's a known
				// issue, that this can lead to a much higher memory consumption then necessary and may also have negative
				// impact on rendering performance.
				for (int p = 0; p < pointCount; ++p)
				{
					int pointIndex = facePointIndices[facePointOffset + p];
					typename Types::Vector3 point = TypeConverter::toVector3(points[pointIndex]);
					typename Types::Vector3 normal;
					typename Types::Vector2 uv;

					// if the mesh provided normals we need to convert them, too
					if (!normals.empty())
					{
						// how the normals are converted depends on their interpolation type
						if (normalInterpolation == pxr::UsdGeomTokens->vertex)
						{
							// normals are authored for each point
							normal = TypeConverter::toVector3(normals[pointIndex]);
						} else if (normalInterpolation == pxr::UsdGeomTokens->faceVarying)
						{
							// normals are authored for each point of a face
							normal = TypeConverter::toVector3(normals[facePointOffset + p]);
						} else if (normalInterpolation == pxr::UsdGeomTokens->uniform)
						{
							// normals are authored for each face only
							normal = TypeConverter::toVector3(normals[faceIndex]);
						}
					} else
					{
						// we need to calculate a normal vector for this point if none are provided in the
						// usdMesh. Omitting normal vectors completely in game engines can lead to rndering
						// artifacts.
						// to keep normal calculation simple we calculate the normal of the point, based on the
						// face it belongs to
						int vidx1 = facePointIndices[facePointOffset + 0];
						int vidx2 = facePointIndices[facePointOffset + 1];
						int vidx3 = facePointIndices[facePointOffset + 2];
						class pxr::GfVec3d edge1 = points[vidx2] - points[vidx1];
						class pxr::GfVec3d edge2 = points[vidx3] - points[vidx1];
						normal = TypeConverter::toVector3(pxr::GfCross(edge1, edge2).GetNormalized());
					}

					// if the mesh provided texture coordinates (uv mapping) convert them, too
					if (!texcoords.empty())
					{
						// texture coordinats need to be handled based on interpolation type
						if (texcoordsInterpolation == pxr::UsdGeomTokens->vertex)
						{
							// uv's are authored for each point
							uv = TypeConverter::toVector2(texcoords[pointIndex]);
						} else if (texcoordsInterpolation == pxr::UsdGeomTokens->faceVarying)
						{
							// uv's are authored for each point per face. In this case the uv's might be
							// shared and thus referenced using the texcoordIndex list
							int uvIndex = texcoordIndices.empty() ? facePointOffset + p : texcoordIndices[facePointOffset + p];
							uv = TypeConverter::toVector2(texcoords[uvIndex]);
						}

						// flip y value
						// TODO: is there a reliable way to know when to flip and when not? Actually it seems always to be required
					    uv = FlipUvV(uv);
					}

				    // if the mesh provided skinning data we will convert them to
				    std::vector<uint32_t> boneIdx;
				    std::vector<float> boneWeight;
				    if (!joint_indices.empty())
				    {
				        // target engines using the bone influences might have limits on how many bone weights
				        // they can handle. Godot for example can handle 4 or 8 bone weights. However, this should be
				        // handled in the target engine specialization of the MeshBuilder
				        for (int element=0; element < joint_index_element_size; ++element)
				        {
				            int joint_element_index = pointIndex * joint_index_element_size + element;
				            int joint_idx = joint_indices[joint_element_index];
				            // if the mesh specifies a custom joint order we calculate the bone index from the given
				            // joint order list (containing the bone path) of the mesh and the skeletons map
				            // otherwise the joint index is taken as is (TODO: may require mapping as well)
				            int bone_idx = joint_idx;
				            if (joint_order.empty())
				            {
				                bone_idx = joint_idx;
				            } else
				            {
				                class pxr::TfToken joint = joint_order[joint_idx];
				                // USD joints are available as names/token only. From the structure of the USD file
				                // it has been ensured, that the skeleton has been converted already and contains a map from joint name
				                // to bone index
				                bone_idx = boneNameToIndex[joint.GetString()];
				            }

				            boneIdx.push_back(bone_idx);
				            boneWeight.push_back(joint_weights[joint_element_index]);
				        }
				    }

					builder.AddVertex(meshData, point, normal, uv, boneIdx, boneWeight);
				}

				// once we converted all vertices of this face we re-construct the index list. While doing so
				// we ensure that we triangulate the face if it contains of more then 3 points
				if (pointCount == 3)
				{
					typename Types::Index vertexCount = builder.GetVertexCount(meshData);
					builder.AddIndex(meshData, vertexCount - 3);
					builder.AddIndex(meshData, vertexCount - 1);
					builder.AddIndex(meshData, vertexCount - 2);
				} else
				{
					// use simple fan triangulation if the current face is spanned from more then 3 points
					typename Types::Index basePointIndex = builder.GetVertexCount(meshData) - pointCount;
					for (int i = 1; i < pointCount - 1; ++i)
					{
						builder.AddIndex(meshData, basePointIndex);
						builder.AddIndex(meshData, basePointIndex + i + 1);
						builder.AddIndex(meshData, basePointIndex + i);
					}
				}
			}

			return meshData;
		}

		// as we don't know the components of the used UV representations we need to specialize this
	    // one for each target engine
	    static typename Types::Vector2 FlipUvV(const typename Types::Vector2& input);
	    
	private:
		// the points of the mesh, as defined in the usd prim
		pxr::VtArray<class pxr::GfVec3f> points;
		// the list of number of points that span the face at index
		pxr::VtArray<int> facePointCounts;
		// list of indices for each point of a face into points
		pxr::VtArray<int> facePointIndices;
		// orientation of faces (order of points) in the mesh (Right- or LeftHanded)
		class pxr::TfToken orientation;
		// as the number of points per face may vary, we store the offset into facePointIndices for
		// fast and convinient access to the correct point index for a specif face
		pxr::VtArray<int> facePointOffsets;
		// list of colors of the mesh. Number of entries varies based on interpolation type.
		// Interpolation could be Face, Point or Constant
		pxr::VtArray<class pxr::GfVec3f> colors;
		class pxr::TfToken colorInterpolation;
		// list of normal vectors. Number of entries varies based on interpolation type.
		// Interpolation could be Face, Edge, Point
		pxr::VtArray<class pxr::GfVec3f> normals;
		class pxr::TfToken normalInterpolation;
		// list of texture coordinates (UV mappings) of the points.
		// Number of entries matches points if no texcoords_indices are provided
		pxr::VtArray<class pxr::GfVec2f> texcoords;
		// list if indices into the texcoords array. If this list is provided the number of entries
		// should match the number of points
		pxr::VtArray<int> texcoordIndices;
		class pxr::TfToken texcoordsInterpolation;

	    // to allow skinning of a skeleton based on this mesh we use the joints (bones) and their attributes
	    pxr::VtArray<class pxr::TfToken> joint_order;
	    pxr::VtArray<int> joint_indices;
	    int joint_index_element_size = 0;
	    pxr::VtArray<float> joint_weights;
	    int joint_weights_element_size = 0;

	    std::map<std::string, int32_t> boneNameToIndex;
	};
}
}