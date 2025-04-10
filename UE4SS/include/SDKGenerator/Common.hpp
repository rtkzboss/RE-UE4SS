#pragma once

#include <File/File.hpp>

namespace RC
{
    namespace Unreal
    {
        class UObject;
        class UClass;
        class UEnum;
        class UScriptStruct;
        class FProperty;
        class UFunction;
        class FFieldClass;
    } // namespace Unreal

    namespace UEGenerator
    {
        // TODO: Deal with the 'MulticastDelegate' type existing and the 'Inline' & 'Sparse' types not existing in earlier UE4 versions (low priority)
        enum class DelegateType
        {
            Delegate,
            MulticastInlineDelegate,
            MulticastSparseDelegate
        };

        enum class EnableForwardDeclarations
        {
            Yes,
            No
        };

        auto is_integral_type(Unreal::FProperty* property) -> bool;
        auto get_native_enum_name(Unreal::UEnum* uenum, bool include_type = true) -> File::StringType;
        auto generate_property_cxx_name(Unreal::FProperty* property,
                                        bool is_top_level_declaration,
                                        Unreal::UObject* class_context,
                                        EnableForwardDeclarations = EnableForwardDeclarations::No) -> File::StringType;
        auto generate_property_lua_name(Unreal::FProperty* property, bool is_top_level_declaration, Unreal::UObject* class_context) -> File::StringType;
        auto sanitize_property_name(const File::StringType& property_name) -> File::StringType;
        auto generate_delegate_name(Unreal::FProperty* property, const File::StringType& context_name) -> File::StringType;
        auto get_native_class_name(Unreal::UClass const* uclass, bool interface_name = false) -> File::StringType;
        auto get_native_struct_name(Unreal::UScriptStruct* script_struct) -> File::StringType;
        auto get_native_field_class_name(Unreal::FFieldClass* fc) -> StringType;
        auto get_native_delegate_type_name(Unreal::UFunction* signature_function, Unreal::UClass* current_class = nullptr, bool strip_outer_name = false)
                -> File::StringType;
        auto is_delegate_signature_function(Unreal::UFunction* signature_function) -> bool;
        auto strip_delegate_signature_postfix(Unreal::UFunction* signature_function) -> File::StringType;
    } // namespace UEGenerator
} // namespace RC
