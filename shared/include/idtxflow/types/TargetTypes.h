#pragma once

/**
 * Define Type concepts and traits to allow game engine type specialization when implementing conversation from
 * openUSD into the target game engine.
 */

#include <vector>
#include <cstdint>
#include <concepts>
#include <type_traits>

namespace idtxflow
{
namespace types
{
	/**
	 * Definition of the different concepts
	 **/
	template<typename T>
	concept Vector4Like = requires(T v)
	{
		{ v.x } -> std::convertible_to<float>;
		{ v.y } -> std::convertible_to<float>;
		{ v.z } -> std::convertible_to<float>;
		{ v.w } -> std::convertible_to<float>;
	} || requires(T v) {
		{ v[0] } -> std::convertible_to<float>;
		{ v[1] } -> std::convertible_to<float>;
		{ v[2] } -> std::convertible_to<float>;
		{ v[3] } -> std::convertible_to<float>;
	};

	template<typename T>
	concept Vector3Like = requires(T v)
	{
		{ v.x } -> std::convertible_to<float>;
		{ v.y } -> std::convertible_to<float>;
		{ v.z } -> std::convertible_to<float>;
	} || requires(T v) {
		{ v[0] } -> std::convertible_to<float>;
		{ v[1] } -> std::convertible_to<float>;
		{ v[2] } -> std::convertible_to<float>;
	};

	template<typename T>
	concept Vector2Like = requires(T v)
	{
		{ v.x } -> std::convertible_to<float>;
		{ v.y } -> std::convertible_to<float>;
	} || requires(T v) {
		{ v[0] } -> std::convertible_to<float>;
		{ v[1] } -> std::convertible_to<float>;
	};

	template<typename T>
	concept ColorLike = requires(T c)
	{
		{ c.r } -> std::convertible_to<float>;
		{ c.g } -> std::convertible_to<float>;
		{ c.b } -> std::convertible_to<float>;
		{ c.a } -> std::convertible_to<float>;
	} || requires(T c)
	{
		{ c.R } -> std::convertible_to<float>;
		{ c.G } -> std::convertible_to<float>;
		{ c.B } -> std::convertible_to<float>;
		{ c.A } -> std::convertible_to<float>;
	};

	template<typename T>
	concept TransformLike = requires(T m) {
		(std::is_class_v<T> && sizeof(T) >= sizeof(float) * 16);
	};

	template<typename T>
	concept IndexLike = requires(T i)
	{
		{ i } -> std::convertible_to<int>;
	};

	// a concept bundeling all target engine specifc types
	template<typename T>
	concept TargetEngineTypesLike = requires
	{
		// Core & Mesh Data Types
		typename T::Vector4;
		typename T::Vector3;
		typename T::Vector2;
	  typename T::Quaternion;
		typename T::Color;
		typename T::Transform;
		typename T::MeshData;
		typename T::Index;

		// Material Data Types
		typename T::Material;
		typename T::Texture;

		// Node Data Types
		typename T::ConvertedEntity; // the type a prim is converted into (most common base type for all node types)
		typename T::ConvertedEntityHandle; // stable handle used to transfer an entity across module boundaries

		{ T::GetConvertedEntityHandle(static_cast<typename T::ConvertedEntity*>(nullptr)) }
			-> std::same_as<typename T::ConvertedEntityHandle>;
		{ T::ResolveConvertedEntity(typename T::ConvertedEntityHandle{}) }
			-> std::same_as<typename T::ConvertedEntity*>;
		typename T::OwningEntity; // the type of the entity "Owning" all converted entitys and act as outermost target/root
		
		requires Vector4Like<typename T::Vector4>;
		requires Vector3Like<typename T::Vector3>;
		requires Vector2Like<typename T::Vector2>;
		requires ColorLike<typename T::Color>;
		requires TransformLike<typename T::Transform>;
		requires IndexLike<typename T::Index>;
	};

	// Specify the TargetEngine type traits
	template<typename TargetEngine>
	struct TargetEngineTypes;

	template<typename TargetEngine>
	concept ValidTargetEngine = requires
	{
		typename TargetEngineTypes<TargetEngine>;
		requires TargetEngineTypesLike<TargetEngineTypes<TargetEngine>>;
	};
}
}

