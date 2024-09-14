#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <File/File.hpp>
#include <SDKGenerator/Common.hpp>
#include <SettingsManager.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FSoftObjectProperty.hpp>
#pragma warning(disable : 4005)
#include <Unreal/NameTypes.hpp>
#pragma warning(default : 4005)

namespace RC::Unreal
{
    class UObject;
    class UFunction;
    class UStruct;
    class FProperty;
    class FNumericProperty;
    class UClass;
    class FField;
    class UEnum;
    class UScriptStruct;
} // namespace RC::Unreal

namespace RC::UEGenerator
{
    using FFilePath = std::filesystem::path;
    using FName = RC::Unreal::FName;
    using UObject = RC::Unreal::UObject;
    using UStruct = RC::Unreal::UStruct;
    using UClass = RC::Unreal::UClass;
    using FProperty = RC::Unreal::FProperty;
    using FNumericProperty = RC::Unreal::FNumericProperty;
    using FObjectProperty = RC::Unreal::FObjectProperty;
    using FSoftObjectPath = RC::Unreal::FSoftObjectPath;
    using FField = RC::Unreal::FField;
    using UEnum = RC::Unreal::UEnum;
    using UScriptStruct = RC::Unreal::UScriptStruct;
    using UFunction = RC::Unreal::UFunction;

    enum class DependencyLevel
    {
        NoDependency,
        /** Object dependency will result in a pre-declaration statement generation */
        PreDeclaration,
        /** Object dependency will result in the header include generation */
        Include,
    };

    enum class AccessModifier
    {
        None,
        Public,
        Protected,
        Private
    };

    struct ClassBlueprintInfo
    {
        bool is_blueprint_type;
        bool is_blueprintable;

        ClassBlueprintInfo() : is_blueprint_type(false), is_blueprintable(false)
        {
        }
    };

    struct PropertyTypeDeclarationContext
    {
        std::wstring context_name;

        class GeneratedSourceFile* source_file;

        bool is_top_level_declaration;
        bool* out_is_bitmask_bool;

        PropertyTypeDeclarationContext(std::wstring_view context_name,
                                       GeneratedSourceFile* source_file = NULL,
                                       bool is_top_level_declaration = false,
                                       bool* out_is_bitmask_bool = NULL)
        {
            this->context_name = context_name;
            this->source_file = source_file;
            this->is_top_level_declaration = is_top_level_declaration;
            this->out_is_bitmask_bool = out_is_bitmask_bool;
        }

        auto inner_context() const -> PropertyTypeDeclarationContext
        {
            return PropertyTypeDeclarationContext(context_name, source_file);
        }
    };

    struct StringInsensitiveCompare
    {
        auto operator()(std::wstring const& a, std::wstring const& b) const -> bool
        {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        }
    };

    using CaseInsensitiveSet = std::unordered_set<FName>;

    class GeneratedFile
    {
      public:
        static constexpr StringViewType INDENT = STR("    ");
      protected:
        std::wstring m_file_base_name;
        FFilePath m_full_file_path;

        std::wstring m_file_contents_buffer;
        int32_t m_current_indent_count;

      public:
        GeneratedFile(const FFilePath& full_file_path);
        virtual ~GeneratedFile() = default;

        // Delete copy and move constructors and assignment operator
        GeneratedFile(const GeneratedFile&) = delete;
        GeneratedFile(GeneratedFile&&) = default;
        auto operator=(const GeneratedFile&) -> void = delete;

        auto append_line() -> void;
        auto append_line(StringViewType line) -> void;
        auto append_line_no_indent(StringViewType line) -> void;

        template <class... Args>
        auto format_line_no_indent(std::wformat_string<Args...> fmt, Args&&... args)
        {
            std::format_to(std::back_inserter(m_file_contents_buffer), fmt, std::forward<Args>(args)...);
            m_file_contents_buffer += '\n';
        }
        template <class... Args>
        auto format_line(std::wformat_string<Args...> fmt, Args&&... args)
        {
            for (int32_t i = 0; i < m_current_indent_count; i++)
            {
                m_file_contents_buffer += INDENT;
            }
            format_line_no_indent(fmt, std::forward<Args>(args)...);
        }

        auto begin_indent_level() -> void;
        auto end_indent_level() -> void;
        auto serialize_file_content_to_disk() -> bool;

        virtual auto has_content_to_save() const -> bool;
        virtual auto generate_file_contents() -> std::wstring;
    };

    struct PropertyAccess
    {
        FProperty* prop;
        int32_t index;
    };
    struct ArrayAccess
    {
        int32_t index;
    };
    struct MapAccess
    {
        FProperty* key_prop;
        void const* key;
    };
    using PropertySpec = std::variant<PropertyAccess, ArrayAccess>;
    class PropertyScope
    {
        std::wstring_view m_root;
        std::vector<std::tuple<FProperty*, int32_t>> m_elements;

      public:
        PropertyScope(std::wstring_view root);
        PropertyScope();
        auto pop() -> void;
        auto push(FProperty* prop, int32_t index) -> void;
        auto push_array(int32_t index) -> void;
        auto access(UStruct* this_struct, GeneratedSourceFile& implementation_file) const -> StringType;
        //auto assign(UStruct* this_struct, StringViewType value, GeneratedSourceFile& implementation_file) const -> StringType;
    };

    class DefaultSubobjects
    {
        std::unordered_map<FName, size_t> objects;
        std::vector<std::pair<FName, UObject*>> objects_order;

      public:
        auto find(FName name) const -> std::pair<UObject*, bool>;
        auto insert_or_update(UObject* value) -> void;
        auto insert_or_update(FName name, UObject* value) -> void;
        auto begin() const -> std::vector<std::pair<FName, UObject*>>::const_iterator
        {
            return objects_order.begin();
        }
        auto end() const -> std::vector<std::pair<FName, UObject*>>::const_iterator
        {
            return objects_order.end();
        }
    };

    class GeneratedSourceFile : public GeneratedFile
    {
      private:
        std::wstring m_file_module_name;
        std::map<UObject*, DependencyLevel> m_dependencies;
        std::set<std::wstring> m_extra_includes;
        mutable std::set<std::wstring> m_dependency_module_names;
        std::unordered_map<FProperty*, uint32_t> m_cons_properties;
        UObject* m_object;
        GeneratedSourceFile* m_header_file;
        bool m_is_implementation_file;
        uint32_t m_gen_id = 1;

      public:
        std::wstring m_implementation_constructor;
        std::unordered_map<UObject const*, int32_t> dso_lookup;
        std::unordered_set<FName> parent_property_names{};
        std::map<FProperty*, std::pair<PropertyScope, StringType>> attachments{};

        GeneratedSourceFile(const FFilePath& file_path, std::wstring_view file_module_name, bool is_implementation_file, UObject* object);

        // Delete copy and move constructors and assignment operator
        GeneratedSourceFile(const GeneratedSourceFile&) = delete;
        GeneratedSourceFile(GeneratedSourceFile&&) = default;
        auto operator=(const GeneratedSourceFile&) -> void = delete;

        auto gen_id() -> uint32_t;

        auto set_header_file(GeneratedSourceFile* header_file) -> void;
        auto add_dependency_object(UObject* object, DependencyLevel dependency_level) -> void;
        auto add_extra_include(std::wstring included_file_name) -> void;

        auto get_header_module_name() const -> std::wstring const&
        {
            return m_file_module_name;
        }
        auto is_implementation_file() const -> bool
        {
            return m_is_implementation_file;
        }

        auto get_current_string_position() -> size_t
        {
            return m_file_contents_buffer.size();
        }
        auto get_corresponding_object() -> UObject*
        {
            return m_object;
        }

        virtual auto has_content_to_save() const -> bool override;

        auto copy_dependency_module_names(std::set<std::wstring>& out_dependency_module_names) const -> void
        {
            out_dependency_module_names.insert(m_dependency_module_names.begin(), m_dependency_module_names.end());
        }

        auto static create_source_file(const FFilePath& root_dir, std::wstring_view module_name, std::wstring const& base_name, bool is_implementation_file, UObject* object)
                -> GeneratedSourceFile;
        virtual auto generate_file_contents() -> std::wstring override;
        auto generate_cons_property(FProperty* prop) -> StringType;

      protected:
        auto has_dependency(UObject* object, DependencyLevel dependency_level) -> bool;

        auto generate_pre_declarations_string() const -> std::wstring;
        auto generate_includes_string() const -> std::wstring;
    };

    struct UniqueName
    {
        static constexpr int32_t HAS_NO_DUPLICATES = 1;

        File::StringType name{};
        int32_t usable_id{HAS_NO_DUPLICATES};
    };

    class PropertyListView
    {
        std::unordered_set<UStruct*> wildcard;
        std::unordered_map<UStruct*, std::unordered_map<FName, SettingsManager::PropertyOp>> ops;

      public:
        PropertyListView(SettingsManager::PropertyList const& list);
        // auto includes(UStruct* owner, FName property_name) -> bool;
        auto includes(FProperty* prop) const -> bool;
    };

    class UEHeaderGenerator
    {
      private:
        FFilePath m_root_directory;
        std::wstring m_primary_module_name;

        std::set<std::wstring> m_forced_module_dependencies;
        std::set<std::wstring> m_ignored_module_names;

        std::unordered_map<UStruct*, void*> m_struct_defaults;
        std::unordered_map<UEnum*, FNumericProperty*> m_underlying_enum_props;
        std::set<UEnum*> m_blueprint_visible_enums;
        std::set<UScriptStruct*> m_blueprint_visible_structs;
        std::unordered_map<UClass*, DefaultSubobjects> m_default_subobjects;
        std::map<std::wstring, std::set<std::wstring>> m_module_dependencies;
        PropertyListView bind_widget;
        PropertyListView ignore_default;

        std::vector<GeneratedSourceFile> m_header_files;
        std::unordered_set<UStruct*> m_structs_that_need_get_type_hash;

        // Storage to ensure that we don't have duplicate file names
        static std::map<File::StringType, UniqueName> m_used_file_names;
        static std::map<UObject*, int32_t> m_dependency_object_to_unique_id;

        // Storage for class defaultsubojects when populating property initializers
        std::unordered_map<FName, FObjectProperty*> m_class_subobjects;

      public:
        UEHeaderGenerator(const FFilePath& root_directory);
        ~UEHeaderGenerator();

        // Delete copy, move and assignment operators
        UEHeaderGenerator(const UEHeaderGenerator&) = delete;
        UEHeaderGenerator(UEHeaderGenerator&&) = delete;
        auto operator=(const UEHeaderGenerator&) -> void = delete;

        auto ignore_selected_modules() -> void;

        auto dump_native_packages() -> void;
        auto preprocess_delegate_signature(UFunction* sig) -> void;
        auto preprocess_class(UClass* uclass) -> void;
        auto preprocess_cdo_property(FProperty* property, void const* data, DefaultSubobjects& dsos) -> void;
        auto preprocess_script_struct(UScriptStruct* ustruct) -> void;
        auto preprocess_struct(UStruct* ustruct) -> void;
        auto preprocess_function(UFunction* func) -> void;
        auto preprocess_property(FProperty* prop, bool blueprint_visible) -> void;
        auto preprocess_hashed_property(FProperty* property) -> void;
        auto generate_object_description_file(UObject* object) -> bool;
        auto generate_module_build_file(std::wstring const& module_name) -> void;
        auto generate_module_implementation_file(std::wstring_view module_name) -> void;

        static auto needs_advanced_access(UStruct* this_struct, FProperty* prop) -> bool;

      private:
        auto is_struct_blueprint_visible(UScriptStruct* ustruct) const -> bool;

        auto generate_interface_definition(UClass* function, GeneratedSourceFile& header_data) -> void;
        auto generate_object_definition(UClass* interface_function, GeneratedSourceFile& header_data) -> void;
        auto generate_struct_definition(UScriptStruct* property, GeneratedSourceFile& header_data) -> void;
        auto generate_enum_definition(UEnum* name_pair, GeneratedSourceFile& header_data) -> void;
        auto generate_global_delegate_declaration(UFunction* signature_function, UClass* delegate_class, GeneratedSourceFile& header_data) -> void;
        auto generate_delegate_type_declaration(UFunction* signature_function, UClass* delagate_class, GeneratedSourceFile& header_data) -> void;
        auto generate_object_implementation(UClass* property, GeneratedSourceFile& implementation_file) -> void;
        auto generate_struct_implementation(UScriptStruct* property, GeneratedSourceFile& implementation_file) -> void;

        auto generate_get_type_hash(UScriptStruct* ustruct, GeneratedSourceFile& header_file) -> void;
        auto generate_property(UStruct* ustruct, FProperty* property, GeneratedSourceFile& header_data) -> void;
        auto generate_function(UClass* uclass,
                               UFunction* function,
                               GeneratedSourceFile& header_data,
                               bool is_generating_interface,
                               CaseInsensitiveSet const& blacklisted_parameter_names,
                               bool generate_as_override = false) -> void;

        auto generate_class_value(UClass* uclass, UClass* metaclass, GeneratedSourceFile& implementation_file) -> StringType;
        auto generate_struct_value(UScriptStruct* value, GeneratedSourceFile& implementation_file) -> StringType;
        auto generate_property_value(UStruct* this_struct, FProperty* property, void const* data, GeneratedSourceFile& implementation_file) -> StringType;
        auto generate_property_element_value(UStruct* this_struct, FProperty* property, void const* data, GeneratedSourceFile& implementation_file) -> StringType;
        auto generate_object_property_element_value(UStruct* this_struct, FProperty* prop, UObject* value, UClass* metaclass, GeneratedSourceFile& implementation_file) -> StringType;
        auto generate_default_property_value(UStruct* this_struct, FProperty* property, GeneratedSourceFile& implementation_file) -> StringType;
        auto generate_default_property_element_value(UStruct* this_struct, FProperty* property, GeneratedSourceFile& implementation_file) -> StringType;
        auto generate_property_assignment_in_container(UStruct* this_struct, FProperty* property, void const* object, void const* archetype, GeneratedSourceFile& implementation_file, PropertyScope& property_scope, bool write_defaults) -> void;
        auto generate_property_assignment(UStruct* this_struct, FProperty* property, void const* data, void const* arch_data, GeneratedSourceFile& implementation_file, PropertyScope& property_scope, bool write_defaults) -> void;
        auto generate_property_element_assignment(UStruct* this_struct, FProperty* property, void const* data, void const* arch_data, GeneratedSourceFile& implementation_file, PropertyScope& property_scope, bool write_defaults) -> void;
        auto generate_function_implementation(UClass* uclass,
                                              UFunction* function,
                                              GeneratedSourceFile& implementation_file,
                                              bool is_generating_interface,
                                              CaseInsensitiveSet const& blacklisted_parameter_names) -> void;
        auto generate_dso_value(UStruct* this_struct, UObject* object, GeneratedSourceFile& implementation_file) -> StringType;

        auto generate_interface_flags(UClass* uinterface) const -> std::wstring;
        auto generate_class_flags(UClass* uclass) const -> std::wstring;
        auto generate_struct_flags(UScriptStruct* script_struct) const -> std::wstring;
        auto generate_enum_flags(UEnum* uenum) const -> std::wstring;
        auto generate_property_type_declaration(FProperty* property, const PropertyTypeDeclarationContext& context) -> std::wstring;
        auto generate_property_flags(UStruct* ustruct, FProperty* property) const -> std::wstring;
        auto generate_function_argument_flags(FProperty* property) const -> std::wstring;
        auto generate_function_flags(UFunction* function, bool is_function_pure_virtual = false) const -> std::wstring;
        auto generate_function_parameter_list(UClass* property,
                                              UFunction* function,
                                              GeneratedSourceFile& header_data,
                                              bool generate_comma_before_name,
                                              std::wstring_view context_name,
                                              CaseInsensitiveSet const& blacklisted_parameter_names,
                                              int32_t* out_num_params = NULL) -> std::wstring;

        auto is_default_value(FProperty* prop, void const* object, void const* archetype) -> bool;
        auto get_default_object(UStruct* ustruct) -> void const*;
        auto generate_soft_path(std::wstring_view kind, FSoftObjectPath const& path) -> std::wstring;
        auto generate_object_finder(UClass* class_, std::wstring_view path, GeneratedSourceFile& implementation_file, bool is_class) -> std::wstring;
        auto generate_enum_value(UEnum* uenum, int64_t enum_value, GeneratedSourceFile& implementation_file) -> StringType;
        auto generate_assignment_expression(UStruct* this_struct,
                                            FProperty* property,
                                            int32_t index,
                                            std::wstring_view value,
                                            GeneratedSourceFile& implementation_file,
                                            PropertyScope& property_scope,
                                            std::wstring_view operator_type = STR(" = ")) -> void;

        auto static generate_parameter_count_string(int32_t parameter_count) -> CharType const*;
        auto static determine_primary_game_module_name() -> std::wstring;

      public:
        auto add_module_and_sub_module_dependencies(std::set<std::wstring_view>& out_module_dependencies, std::wstring const& module_name)
                -> void;
        auto static collect_blacklisted_parameter_names(UStruct* property, bool skip_self) -> CaseInsensitiveSet;

        auto static generate_object_pre_declaration(UObject* object) -> std::vector<std::vector<std::wstring>>;

        auto static convert_module_name_to_api_name(std::wstring module_name) -> std::wstring;
        auto static get_module_name_for_package(UObject* package) -> std::wstring;
        auto static sanitize_enumeration_name(std::wstring enumeration_name) -> std::wstring;
        auto static get_highest_enum(UEnum* uenum) -> int64_t;
        auto static get_lowest_enum(UEnum* uenum) -> int64_t;

        auto static get_class_blueprint_info(UClass* function) -> ClassBlueprintInfo;

        auto static append_access_modifier(GeneratedSourceFile& header_data, AccessModifier needed_access, AccessModifier& current_access) -> void;
        auto static get_property_access_modifier(FProperty* property) -> AccessModifier;
        auto static get_function_access_modifier(UFunction* function) -> AccessModifier;
        auto static create_string_literal(std::wstring_view string) -> std::wstring;
        auto static get_header_name_for_object(UObject* object, bool get_existing_header = false) -> std::wstring;
        auto static generate_cross_module_include(UObject* object, std::wstring_view module_name, std::wstring_view fallback_name) -> std::wstring;
    };
} // namespace RC::UEGenerator
