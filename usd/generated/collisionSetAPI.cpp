//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "./collisionSetAPI.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/typed.h"

#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_OPEN_SCOPE

// Register the schema with the TfType system.
TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<IDTXCollisionSetAPI,
        TfType::Bases< UsdAPISchemaBase > >();
    
}

/* virtual */
IDTXCollisionSetAPI::~IDTXCollisionSetAPI()
{
}

/* static */
IDTXCollisionSetAPI
IDTXCollisionSetAPI::Get(const UsdStagePtr &stage, const SdfPath &path)
{
    if (!stage) {
        TF_CODING_ERROR("Invalid stage");
        return IDTXCollisionSetAPI();
    }
    return IDTXCollisionSetAPI(stage->GetPrimAtPath(path));
}


/* virtual */
UsdSchemaKind IDTXCollisionSetAPI::_GetSchemaKind() const
{
    return IDTXCollisionSetAPI::schemaKind;
}

/* static */
bool
IDTXCollisionSetAPI::CanApply(
    const UsdPrim &prim, std::string *whyNot)
{
    return prim.CanApplyAPI<IDTXCollisionSetAPI>(whyNot);
}

/* static */
IDTXCollisionSetAPI
IDTXCollisionSetAPI::Apply(const UsdPrim &prim)
{
    if (prim.ApplyAPI<IDTXCollisionSetAPI>()) {
        return IDTXCollisionSetAPI(prim);
    }
    return IDTXCollisionSetAPI();
}

/* static */
const TfType &
IDTXCollisionSetAPI::_GetStaticTfType()
{
    static TfType tfType = TfType::Find<IDTXCollisionSetAPI>();
    return tfType;
}

/* static */
bool 
IDTXCollisionSetAPI::_IsTypedSchema()
{
    static bool isTyped = _GetStaticTfType().IsA<UsdTyped>();
    return isTyped;
}

/* virtual */
const TfType &
IDTXCollisionSetAPI::_GetTfType() const
{
    return _GetStaticTfType();
}

UsdRelationship
IDTXCollisionSetAPI::GetPhysicsColliderRel() const
{
    return GetPrim().GetRelationship(IDTXTokens->physicsCollider);
}

UsdRelationship
IDTXCollisionSetAPI::CreatePhysicsColliderRel() const
{
    return GetPrim().CreateRelationship(IDTXTokens->physicsCollider,
                       /* custom = */ false);
}

UsdRelationship
IDTXCollisionSetAPI::GetPhysicsColliderQuerryRel() const
{
    return GetPrim().GetRelationship(IDTXTokens->physicsColliderQuerry);
}

UsdRelationship
IDTXCollisionSetAPI::CreatePhysicsColliderQuerryRel() const
{
    return GetPrim().CreateRelationship(IDTXTokens->physicsColliderQuerry,
                       /* custom = */ false);
}

/*static*/
const TfTokenVector&
IDTXCollisionSetAPI::GetSchemaAttributeNames(bool includeInherited)
{
    static TfTokenVector localNames;
    static TfTokenVector allNames =
        UsdAPISchemaBase::GetSchemaAttributeNames(true);

    if (includeInherited)
        return allNames;
    else
        return localNames;
}

PXR_NAMESPACE_CLOSE_SCOPE

// ===================================================================== //
// Feel free to add custom code below this line. It will be preserved by
// the code generator.
//
// Just remember to wrap code in the appropriate delimiters:
// 'PXR_NAMESPACE_OPEN_SCOPE', 'PXR_NAMESPACE_CLOSE_SCOPE'.
// ===================================================================== //
// --(BEGIN CUSTOM CODE)--
