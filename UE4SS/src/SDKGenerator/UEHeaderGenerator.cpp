#define NOMINMAX
#include <Windows.h>
#ifdef TEXT
#undef TEXT
#endif

#include <algorithm>
#include <format>
#include <fstream>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <cstdlib>

#include <SDKGenerator/UEHeaderGenerator.hpp>
#pragma warning(disable : 4005)
#include <DynamicOutput/DynamicOutput.hpp>
#include <UE4SSProgram.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Core/Containers/ScriptArray.hpp>
#include <Unreal/Core/Containers/Set.hpp>
#include <Unreal/Core/Containers/Map.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FClassProperty.hpp>
#include <Unreal/Property/FDelegateProperty.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Property/FFieldPathProperty.hpp>
#include <Unreal/Property/FInterfaceProperty.hpp>
#include <Unreal/Property/FLazyObjectProperty.hpp>
#include <Unreal/Property/FMapProperty.hpp>
#include <Unreal/Property/FMulticastInlineDelegateProperty.hpp>
#include <Unreal/Property/FMulticastSparseDelegateProperty.hpp>
#include <Unreal/Property/FNameProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FSetProperty.hpp>
#include <Unreal/Property/FStrProperty.hpp>
#include <Unreal/Property/FSoftClassProperty.hpp>
#include <Unreal/Property/FSoftObjectProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FTextProperty.hpp>
#include <Unreal/Property/FWeakObjectProperty.hpp>
#include <Unreal/Property/NumericPropertyTypes.hpp>
#include <Unreal/UActorComponent.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UEnum.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UInterface.hpp>
#include <Unreal/UPackage.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/Core/Containers/ScriptArray.hpp>
#pragma warning(default : 4005)

#ifdef _MSC_VER
#define ALIGNED_ALLOC(alignment, size) _aligned_malloc(size, alignment)
#define ALIGNED_FREE(mem) _aligned_free(mem)
#else
#define ALIGNED_ALLOC(alignment, size) ::std::aligned_alloc(alignment, size)
#define ALIGNED_FREE(mem) ::std::free(mem)
#endif

namespace RC::UEGenerator
{
    using namespace RC::Unreal;

    static auto is_subtype_valid(FProperty* property) -> bool;
    static auto is_subtype_struct_valid(UScriptStruct* subtype) -> bool
    {
        static const std::vector<FName> invalid_subtype_structs{
                FName(STR("FloatInterval"), FNAME_Add),
                FName(STR("SplineCurves"), FNAME_Add),
                FName(STR("Int32Interval"), FNAME_Add),
                FName(STR("BoneReference"), FNAME_Add),
                FName(STR("OverlapResult"), FNAME_Add),
                FName(STR("RichCurve"), FNAME_Add),
                FName(STR("MovieScenePropertyTrack"), FNAME_Add),
                FName(STR("MovieSceneSection"), FNAME_Add),
                FName(STR("MovieSceneFloatChannel"), FNAME_Add),
                FName(STR("MovieSceneActorReferenceData"), FNAME_Add),
                FName(STR("ExpressionInput"), FNAME_Add),
                FName(STR("ScalarParameterNameAndCurve"), FNAME_Add),
                FName(STR("PredictionKey"), FNAME_Add),
                FName(STR("CustomPrimitiveData"), FNAME_Add),
                FName(STR("EQSParametrizedQueryExecutionRequest"), FNAME_Add),
                FName(STR("BlendFilter"), FNAME_Add),
                FName(STR("AIDataProviderFloatValue"), FNAME_Add),
                FName(STR("AIDataProviderIntValue"), FNAME_Add),
                FName(STR("AIDataProviderBoolValue"), FNAME_Add),
                FName(STR("EnvTraceData"), FNAME_Add),
                FName(STR("Timeline"), FNAME_Add),
                FName(STR("AIMoveRequest"), FNAME_Add),
                FName(STR("NavAgentSelector"), FNAME_Add),
        };

        auto subtype_name = subtype->GetNamePrivate();
        for (auto subtype_struct_name : invalid_subtype_structs)
        {
            if (subtype_name == subtype_struct_name) return false;
        }
        return true;
    }
    static auto is_subtype_inner_valid(FProperty* property) -> bool
    {
        return !property->IsA<FWeakObjectProperty>() && is_subtype_valid(property);
    }
    static auto is_subtype_valid(FProperty* property) -> bool
    {
        if (property->IsA<FNumericProperty>())
        {
            return property->IsA<FIntProperty>() || property->IsA<FInt64Property>() || property->IsA<FFloatProperty>() || property->IsA<FByteProperty>();
        }
        if (auto prop = CastField<FArrayProperty>(property))
        {
            return is_subtype_inner_valid(prop->GetInner());
        }
        if (auto prop = CastField<FSetProperty>(property))
        {
            return is_subtype_inner_valid(prop->GetElementProp());
        }
        if (auto prop = CastField<FMapProperty>(property))
        {
            return is_subtype_inner_valid(prop->GetKeyProp()) && is_subtype_inner_valid(prop->GetValueProp());
        }
        if (auto prop = CastField<FStructProperty>(property))
        {
            return is_subtype_struct_valid(prop->GetStruct());
        }
        if (auto prop = CastField<FEnumProperty>(property))
        {
            // TODO: inaccurate
            return prop->GetUnderlyingProperty()->IsA<FByteProperty>();
        }
        // TODO: ???
        return true;
    }

    static auto string_to_uppercase(std::wstring s) -> std::wstring
    {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
            return towupper(c);
        });
        return s;
    }
    static auto string_contains_ci(std::wstring_view haystack, std::wstring_view needle) -> bool
    {
        return std::search(haystack.cbegin(), haystack.cend(), needle.cbegin(), needle.cend(), [](wchar_t a, wchar_t b) {
                   return std::towlower(a) == std::towlower(b);
               }) != haystack.end();
    }

    static auto is_cpp_keyword(std::wstring_view s) -> bool
    {
        static const std::unordered_set<std::wstring_view> kws = { STR("alignas"), STR("alignof"), STR("and"), STR("and_eq"), STR("asm"), STR("atomic_cancel"), STR("atomic_commit"), STR("atomic_noexcept"), STR("auto"), STR("bitand"), STR("bitor"), STR("bool"), STR("break"), STR("case"), STR("catch"), STR("char"), STR("char16_t"), STR("char32_t"), STR("char8_t"), STR("class"), STR("co_await"), STR("co_return"), STR("co_yield"), STR("compl"), STR("concept"), STR("const"), STR("const_cast"), STR("consteval"), STR("constexpr"), STR("constinit"), STR("continue"), STR("decltype"), STR("default"), STR("delete"), STR("do"), STR("double"), STR("dynamic_cast"), STR("else"), STR("enum"), STR("explicit"), STR("export"), STR("extern"), STR("false"), STR("float"), STR("for"), STR("friend"), STR("goto"), STR("if"), STR("inline"), STR("int"), STR("long"), STR("mutable"), STR("namespace"), STR("new"), STR("noexcept"), STR("not"), STR("not_eq"), STR("nullptr"), STR("operator"), STR("or"), STR("or_eq"), STR("private"), STR("protected"), STR("public"), STR("reflexpr"), STR("register"), STR("reinterpret_cast"), STR("requires"), STR("return"), STR("short"), STR("signed"), STR("sizeof"), STR("static"), STR("static_assert"), STR("static_cast"), STR("struct"), STR("switch"), STR("synchronized"), STR("template"), STR("this"), STR("thread_local"), STR("throw"), STR("true"), STR("try"), STR("typedef"), STR("typeid"), STR("typename"), STR("union"), STR("unsigned"), STR("using"), STR("virtual"), STR("void"), STR("volatile"), STR("wchar_t"), STR("while"), STR("xor"), STR("xor_eq"),
          // TODO: remove these, fix for JSON names messing up display index
          STR("index"), STR("result")
        };
        return kws.contains(s);
    }
    static auto fix_cpp_keyword_name(std::wstring& s) -> std::wstring&
    {
        if (!is_cpp_keyword(s)) return s;
        s[0] = s[0] - 'a' + 'A';
        return s;
    }
    static auto fix_cpp_keyword_name(std::wstring&& s) -> std::wstring
    {
        fix_cpp_keyword_name(s);
        return s;
    }

    static auto class_has_config(UClass* uclass) -> bool
    {
        return uclass->HasAnyClassFlags(CLASS_Config | CLASS_DefaultConfig | CLASS_GlobalUserConfig | CLASS_ProjectUserConfig);
    }

    static size_t align_to(size_t size, size_t align)
    {
        return (size + align - 1) & ~align;
    }
    static auto each_item(FScriptArray const& array, FProperty* inner_prop)
    {
        auto start = static_cast<char const*>(array.GetData());
        auto size = inner_prop->GetSize();
        return std::views::iota(0, array.Num()) | std::views::transform([=](auto i) {
                   return start + i * size;
               });
    }
    static auto each_item(FScriptSet const& set, FProperty* element_prop)
    {
        auto layout = FScriptSet::GetScriptLayout(element_prop->GetSize(), element_prop->GetMinAlignment());
        return std::views::iota(0, set.GetMaxIndex()) | std::views::filter([=](auto i) {
                   return set.IsValidIndex(i);
               }) |
               std::views::transform([=](auto i) {
                   return set.GetData(i, layout);
               });
    }
    static auto each_item(FScriptSet const& map, FProperty* key_prop, FProperty* value_prop)
    {
        auto value_offset = value_prop->GetOffset_Internal();
        auto pair_align = std::max(key_prop->GetMinAlignment(), value_prop->GetMinAlignment());
        auto pair_size = align_to(value_offset + value_prop->GetSize(), pair_align);
        auto layout = FScriptSet::GetScriptLayout(pair_size, pair_align);
        return std::views::iota(0, map.GetMaxIndex()) | std::views::filter([=](auto i) {
                   return map.IsValidIndex(i);
               }) |
               std::views::transform([=](auto i) {
                   auto key = map.GetData(i, layout);
                   return std::pair(key, static_cast<void const*>(static_cast<char const*>(key) + value_offset));
               });
    }

    class FlagFormatHelper
    {
        std::set<std::wstring> m_switches;
        std::map<std::wstring, std::set<std::wstring>> m_parameters;
        std::shared_ptr<FlagFormatHelper> m_meta_helper;

        FlagFormatHelper(bool is_root_helper)
        {
            if (is_root_helper)
            {
                m_meta_helper = std::shared_ptr<FlagFormatHelper>(new FlagFormatHelper(false));
            }
        }

      public:
        FlagFormatHelper() : FlagFormatHelper(true)
        {
        }

        auto add_switch(std::wstring const& switch_name) -> void
        {
            m_switches.insert(switch_name);
        }

        auto add_parameter(std::wstring const& parameter_name, std::wstring const& parameter_value) -> void
        {
            if (parameter_name == STR("meta"))
            {
                throw std::invalid_argument("Use get_meta() to add metadata to the flag declaration");
            }

            auto map_iterator = m_parameters.find(parameter_name);
            if (map_iterator != m_parameters.end())
            {
                map_iterator->second.insert(parameter_name);
            }
            else
            {
                m_parameters.insert({parameter_name, {parameter_value}});
            }
        }

        auto get_meta() const -> FlagFormatHelper*
        {
            return m_meta_helper.get();
        }

        auto build_flag_string() const -> std::wstring
        {
            std::wstring resulting_string;

            for (std::wstring const& switch_name : m_switches)
            {
                resulting_string.append(switch_name);
                resulting_string.append(STR(", "));
            }

            for (const auto& parameter_pair : m_parameters)
            {
                resulting_string.append(parameter_pair.first);
                resulting_string.append(STR("="));
                const std::set<std::wstring>& parameter_values = parameter_pair.second;

                if (parameter_values.size() != 1)
                {
                    resulting_string.append(STR("("));

                    for (std::wstring const& parameter_value : parameter_values)
                    {
                        write_escaped(resulting_string, parameter_value);
                        resulting_string.append(STR(", "));
                    }

                    if (parameter_values.size() != 0)
                    {
                        resulting_string.erase(resulting_string.size() - 1, 1);
                    }
                    resulting_string.append(STR(")"));
                }
                else
                {
                    write_escaped(resulting_string, *parameter_values.begin());
                }
                resulting_string.append(STR(", "));
            }

            if (m_meta_helper)
            {
                const std::wstring meta_flag_string = m_meta_helper->build_flag_string();
                if (!meta_flag_string.empty())
                {
                    resulting_string.append(STR("meta=("));
                    resulting_string.append(meta_flag_string);
                    resulting_string.append(STR(")"));
                    resulting_string.append(STR(", "));
                }
            }

            if (!resulting_string.empty())
            {
                resulting_string.erase(resulting_string.size() - 2, 2);
            }
            return resulting_string;
        }
        auto write_escaped(std::wstring& dst, std::wstring_view src) const -> void
        {
            // FBaseParser::ReadNewStyleValue uses FBaseParser::GetToken
            // escape everything that isn't purely alphabetic, which is slightly conservative but not wrong
            if (std::all_of(src.begin(), src.end(), [](auto c) {
                    return 'a' <= c && c <= 'z' || 'A' <= c && c <= 'Z' || '_' == c;
                }))
            {
                dst.append(src);
            }
            else
            {
                dst += '"';
                for (auto c : src)
                {
                    if (c == '"') dst += '\\';
                    dst += c;
                }
                dst += '"';
            }
        }
    };

    auto write_property_name(std::wstring& out, FProperty* prop) -> void
    {
        static FName NAME_Position{STR("Position"), FNAME_Add};
        FName name = prop->GetFName();
        if (name == NAME_Position)
        {
            out.append(STR("Position"));
        }
        else
        {
            out.append(fix_cpp_keyword_name(name.ToString()));
        }
        if (prop->GetPropertyFlags() & CPF_Deprecated)
        {
            out.append(STR("_DEPRECATED"));
        }
    }
    PropertyScope::PropertyScope() : PropertyScope(STR("(*this)"))
    {
    }
    PropertyScope::PropertyScope(std::wstring_view root) : m_root(root)
    {
    }
    auto PropertyScope::pop() -> void
    {
        m_elements.pop_back();
    }
    auto PropertyScope::push(FProperty* prop, int32_t index) -> void
    {
        m_elements.emplace_back(prop, prop->GetArrayDim() == 1 ? -1 : index);
    }
    auto PropertyScope::push_array(int32_t index) -> void
    {
        m_elements.emplace_back(nullptr, index);
    }
    static auto generate_find_property(FProperty* prop, GeneratedSourceFile& implementation_file) -> StringType
    {
        auto owner = prop->GetOutermostOwner();
        if (auto owner_class = Cast<UClass>(owner); owner_class && owner_class->HasAnyClassFlags(CLASS_Native))
        {
            implementation_file.add_dependency(owner_class, DependencyLevel::Include);
            return fmt::format(STR("{}::StaticClass()->FindPropertyByName(\"{}\")"), get_native_class_name(owner_class), prop->GetName());
        }
        static UClass* UUserDefinedStruct_StaticClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.UserDefinedStruct"));
        // NoExport structs don't have STRUCT_Native...
        if (auto owner_struct = Cast<UScriptStruct>(owner); owner_struct && !owner_struct->IsA(UUserDefinedStruct_StaticClass))
        {
            implementation_file.add_dependency(owner_struct, DependencyLevel::Include);
            return fmt::format(STR("TBaseStructure<{}>::Get()->FindPropertyByName(\"{}\")"), get_native_struct_name(owner_struct), prop->GetName());
        }
        Output::send<LogLevel::Error>(STR("Unimplemented advanced access for {}\n"), owner->GetFullName());
        return fmt::format(STR("/*{}*/"), prop->GetFullName());
    }
    auto GeneratedSourceFile::generate_cons_property(FProperty* prop) -> StringType
    {
        auto& id = m_cons_properties[prop];
        if (!id)
        {
            id = gen_id();
            format_line(STR("auto gen{} = {};"), id, generate_find_property(prop, *this));
        }
        return fmt::format(STR("gen{}"), id);
    }
    auto PropertyScope::access(UStruct* this_struct, GeneratedSourceFile& implementation_file) const -> StringType
    {
        StringType out{m_root};
        for (auto [prop, index] : m_elements)
        {
            if (prop && UEHeaderGenerator::needs_advanced_access(this_struct, prop))
            {
                int32_t i = index == -1 ? 0 : index;
                auto prop_str = implementation_file.generate_cons_property(prop);
                out = fmt::format(STR("(*{}->ContainerPtrToValuePtr<{}>(&{}, {}))"), prop_str, generate_property_cxx_name(prop, true, this_struct), out, i);
            }
            else
            {
                if (prop)
                {
                    out.push_back('.');
                    write_property_name(out, prop);
                }
                if (index != -1)
                {
                    fmt::format_to(std::back_inserter(out), "[{}]", index);
                }
            }
        }
        return out;
    }

    auto UEHeaderGenerator::generate_module_build_file(UPackage* package, StringViewType module_name, bool is_primary) -> void
    {
        const FFilePath module_file_path = m_root_directory / module_name / fmt::format(STR("{}.Build.cs"), module_name);
        GeneratedFile module_build_file = GeneratedFile(module_file_path);

        std::set<StringType> public_deps;
        std::set<StringType> private_deps;
        for (auto const& module_name : m_forced_module_dependencies)
        {
            public_deps.insert(module_name);
        }
        add_module_and_sub_module_dependencies(public_deps, private_deps, package);

        module_build_file.append_line(STR("using UnrealBuildTool;"));
        module_build_file.append_line();
        module_build_file.append_line(fmt::format(STR("public class {} : ModuleRules {{"), module_name));
        module_build_file.begin_indent_level();

        module_build_file.append_line(fmt::format(STR("public {}(ReadOnlyTargetRules Target) : base(Target) {{"), module_name));
        module_build_file.begin_indent_level();

        module_build_file.append_line(STR("PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;"));
        if (Version::IsAtLeast(4, 24))
        {
            module_build_file.append_line(STR("bLegacyPublicIncludePaths = false;"));
            module_build_file.append_line(STR("ShadowVariableWarningLevel = WarningLevel.Warning;"));
        }

        module_build_file.append_line();
        module_build_file.append_line(STR("PublicDependencyModuleNames.AddRange(new string[] {"));
        module_build_file.begin_indent_level();
        for (auto const& dep_name : public_deps)
        {
            module_build_file.format_line(STR("\"{}\","), dep_name);
        }
        module_build_file.end_indent_level();
        module_build_file.append_line(STR("});"));

        // TODO: better system for unreflected implicit dependencies
        if (public_deps.contains(STR("OnlineSubsystemUtils")))
        {
            // e.g. FBlueprintSessionResult
            private_deps.insert(STR("OnlineSubsystem"));
        }
        if (!private_deps.empty())
        {
            module_build_file.append_line();
            module_build_file.append_line(STR("PrivateDependencyModuleNames.AddRange(new string[] {"));
            module_build_file.begin_indent_level();
            for (auto const& dep_name : private_deps)
            {
                module_build_file.format_line(STR("\"{}\","), dep_name);
            }
            module_build_file.end_indent_level();
            module_build_file.append_line(STR("});"));
        }

        module_build_file.end_indent_level();
        module_build_file.append_line(STR("}"));

        module_build_file.end_indent_level();
        module_build_file.append_line(STR("}"));

        module_build_file.serialize_file_content_to_disk(*this);
    }

    auto UEHeaderGenerator::generate_module_implementation_file(UPackage* package, StringViewType module_name, bool is_primary) -> void
    {
        FFilePath module_file_path = m_root_directory / module_name / STR("Private") / fmt::format(STR("{}Module.cpp"), module_name);
        GeneratedFile module_impl_file{std::move(module_file_path)};

        module_impl_file.append_line(STR("#include \"Modules/ModuleManager.h\""));
        module_impl_file.append_line();
        if (is_primary)
        {
            module_impl_file.format_line(STR("IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, {}, \"{}\");"), module_name, module_name);
        }
        else
        {
            module_impl_file.format_line(STR("IMPLEMENT_MODULE(FDefaultGameModuleImpl, {});"), module_name);
        }

        module_impl_file.serialize_file_content_to_disk(*this);
    }

    auto UEHeaderGenerator::is_struct_blueprint_visible(UScriptStruct* ustruct) const -> bool
    {
        for (; ustruct; ustruct = ustruct->GetSuperScriptStruct())
        {
            if (m_blueprint_visible_structs.contains(ustruct)) return true;
        }
        return false;
    }

    auto UEHeaderGenerator::generate_interface_definition(UClass* uclass, GeneratedSourceFile& header_data) -> void
    {
        auto interface_class_native_name = get_native_class_name(uclass);
        auto interface_flags_string = generate_interface_flags(uclass);

        StringType maybe_api_name;
        if (uclass->HasAnyClassFlags(CLASS_RequiredAPI))
        {
            maybe_api_name = convert_module_name_to_api_name(get_module_name_for_package(header_data.get_package()));
            maybe_api_name += ' ';
        }

        auto super_class = uclass->GetSuperClass();
        header_data.add_dependency(super_class, DependencyLevel::Include);

        StringType parent_interface_class_name = get_native_class_name(super_class);

        // Generate interface UCLASS declaration
        header_data.append_line(fmt::format(STR("UINTERFACE({})"), interface_flags_string));
        header_data.append_line(fmt::format(STR("class {}{} : public {} {{"), maybe_api_name, interface_class_native_name, parent_interface_class_name));

        header_data.begin_indent_level();
        header_data.append_line(STR("GENERATED_BODY()"));
        header_data.end_indent_level();

        header_data.append_line(STR("};"));
        header_data.append_line();

        // Generate interface real class declaration
        auto interface_native_name = get_native_class_name(uclass, true);
        auto parent_interface_name = get_native_class_name(super_class, true);

        header_data.append_line(fmt::format(STR("class {}{} : public {} {{"), maybe_api_name, interface_native_name, parent_interface_name));
        header_data.begin_indent_level();

        header_data.append_line(STR("GENERATED_BODY()"));

        AccessModifier current_access_modifier = AccessModifier::None;
        append_access_modifier(header_data, AccessModifier::Public, current_access_modifier);

        // Generate delegate type declarations for the current class
        int32_t NumDelegatesGenerated = 0;
        for (UFunction* function : uclass->ForEachFunction())
        {
            if (is_delegate_signature_function(function))
            {
                generate_delegate_type_declaration(function, uclass, header_data);
                ++NumDelegatesGenerated;
            }
        }
        if (NumDelegatesGenerated)
        {
            header_data.append_line();
        }

        // Generate interface functions
        for (UFunction* function : uclass->ForEachFunction())
        {
            if (!is_delegate_signature_function(function))
            {
                append_access_modifier(header_data, get_function_access_modifier(function), current_access_modifier);
                generate_function(uclass, function, header_data, true, CaseInsensitiveSet{});
            }
        }

        header_data.end_indent_level();
        header_data.append_line(STR("};"));
    }

    auto generate_virtual_body(std::wstring_view name, std::wstring_view ret_stmt, bool is_pure_virtual) -> std::wstring
    {
        return is_pure_virtual ? fmt::format(STR("PURE_VIRTUAL({}, {});"), name, ret_stmt) : fmt::format(STR("{{ {} }}"), ret_stmt);
    }
    static auto should_generate_fobjectinitializer_constructor(UClass* uclass) -> bool
    {
        return uclass->IsChildOf<AActor>() || uclass->IsChildOf<UActorComponent>();
    }
    static auto needs_fobjectinitializer_constructor(UClass* uclass) -> bool
    {
        static UClass* UUserWidget_StaticClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.UserWidget"));
        static std::unordered_set<UClass*> classes = {
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/AIModule.AITask")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/OnlineSubsystemUtils.IpNetDriver")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/AnimationBudgetAllocator.SkeletalMeshComponentBudgeted")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.SoundWaveProcedural")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MovieScene.MovieSceneTrack")),
                UUserWidget_StaticClass,
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.DynamicEntryBoxBase")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.ListView")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.ListViewBase")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.RichTextBlock")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.RichTextBlockDecorator")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.RichTextBlockImageDecorator")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TreeView")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.TileView")),
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/CommonUI.CommonTileView")),
        };
        return classes.contains(uclass);
    }
    auto UEHeaderGenerator::generate_object_definition(UClass* uclass, GeneratedSourceFile& header_data) -> void
    {
        const std::wstring class_native_name = get_native_class_name(uclass);
        const std::wstring class_flags_string = generate_class_flags(uclass);

        std::wstring maybe_api_name;
        if ((uclass->GetClassFlags() & CLASS_RequiredAPI) != 0)
        {
            maybe_api_name.append(convert_module_name_to_api_name(get_module_name_for_package(header_data.get_package())));
            maybe_api_name.append(STR(" "));
        }

        UClass* super_class = uclass->GetSuperClass();
        std::wstring parent_class_name;
        if (super_class)
        {
            header_data.add_dependency(super_class, DependencyLevel::Include);
            parent_class_name = get_native_class_name(super_class);
        }
        else
        {
            parent_class_name = STR("UObjectBaseUtility");
        }

        std::wstring interface_list_string;
        bool is_abstract = uclass->GetClassFlags() & CLASS_Abstract;
        auto const& implemented_interfaces = uclass->GetInterfaces();

        for (const RC::Unreal::FImplementedInterface& uinterface : implemented_interfaces)
        {
            header_data.add_dependency(uinterface.Class, DependencyLevel::Include);
            const std::wstring interface_name = get_native_class_name(uinterface.Class, true);

            interface_list_string.append(STR(", public "));
            interface_list_string.append(interface_name);
        }

        header_data.append_line(fmt::format(STR("UCLASS({})"), class_flags_string));
        header_data.append_line(fmt::format(STR("class {}{} : public {}{} {{"), maybe_api_name, class_native_name, parent_class_name, interface_list_string));
        header_data.begin_indent_level();

        header_data.append_line(STR("GENERATED_BODY()"));

        AccessModifier current_access_modifier = AccessModifier::None;
        append_access_modifier(header_data, AccessModifier::Public, current_access_modifier);

        CaseInsensitiveSet blacklisted_parameter_names = collect_blacklisted_parameter_names(uclass, true);
        bool encountered_replicated_properties = false;

        // Generate delegate type declarations for the current class
        int32_t NumDelegatesGenerated = 0;
        for (UFunction* function : uclass->ForEachFunction())
        {
            if (is_delegate_signature_function(function))
            {
                generate_delegate_type_declaration(function, uclass, header_data);
                NumDelegatesGenerated++;
            }
        }
        if (NumDelegatesGenerated)
        {
            header_data.append_line();
        }

        // Generate constructor
        append_access_modifier(header_data, AccessModifier::Public, current_access_modifier);
        CharType const* constructor_args = should_generate_fobjectinitializer_constructor(uclass) ? STR("const FObjectInitializer& ObjectInitializer") : STR("");
        header_data.append_line(fmt::format(STR("{}({});"), class_native_name, constructor_args));
        header_data.append_line();

        // Generate functions
        // we must do this *before* properties b/c UHT will only let properties shadow function parameters, not vice versa
        std::unordered_set<FName> implemented_functions;
        for (UFunction* function : uclass->ForEachFunction())
        {
            if (!is_delegate_signature_function(function))
            {
                append_access_modifier(header_data, get_function_access_modifier(function), current_access_modifier);
                generate_function(uclass, function, header_data, false, blacklisted_parameter_names);
                implemented_functions.emplace(function->GetNamePrivate());
            }
        }

        // Generate properties
        for (FProperty* property : uclass->ForEachProperty())
        {
            encountered_replicated_properties |= property->HasAnyPropertyFlags(CPF_Net);
            append_access_modifier(header_data, get_property_access_modifier(property), current_access_modifier);
            generate_property(uclass, property, header_data);
        }

        // Generate GetLifetimeReplicatedProps override if we have encountered replicated properties
        if (encountered_replicated_properties)
        {
            append_access_modifier(header_data, AccessModifier::Public, current_access_modifier);
            header_data.append_line(STR("virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;"));
            header_data.append_line();
        }

        // Generate overrides for all inherited virtual functions
        if (implemented_interfaces.Num() > 0)
        {
            header_data.append_line();
            header_data.append_line(STR("// Fix for true pure virtual functions not being implemented"));
        }
        for (const RC::Unreal::FImplementedInterface& uinterface : implemented_interfaces)
        {
            for (UFunction* interface_function : uinterface.Class->ForEachFunctionInChain())
            {
                bool should_skip = (interface_function->GetFunctionFlags() & FUNC_BlueprintEvent);

                if (!implemented_functions.contains(interface_function->GetNamePrivate()) && !should_skip)
                {
                    append_access_modifier(header_data, get_function_access_modifier(interface_function), current_access_modifier);
                    generate_function(uclass, interface_function, header_data, false, blacklisted_parameter_names, true);
                }
            }
            // a bunch of non-reflected functions
            for (auto klass = uinterface.Class; klass; klass = klass->GetSuperClass())
            {
                auto name = klass->GetNamePrivate();
                static const auto NAME_MovieSceneTrackTemplateProducer = FName(STR("MovieSceneTrackTemplateProducer"), FNAME_Add);
                if (name == NAME_MovieSceneTrackTemplateProducer)
                {
                    static auto MovieSceneEvalTemplatePtr =
                            UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/MovieScene.MovieSceneEvalTemplatePtr"));
                    header_data.add_dependency(MovieSceneEvalTemplatePtr, DependencyLevel::Include);
                    header_data.append_line(fmt::format(
                            STR("virtual FMovieSceneEvalTemplatePtr CreateTemplateForSection(const UMovieSceneSection& InSection) const override {}"),
                            generate_virtual_body(STR("CreateTemplateForSection"), STR("return {};"), is_abstract)));
                }
                static const auto NAME_AbilitySystemInterface = FName(STR("AbilitySystemInterface"), FNAME_Add);
                if (name == NAME_AbilitySystemInterface)
                {
                    static auto AbilitySystemComponent =
                            UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/GameplayAbilities.AbilitySystemComponent"));
                    header_data.add_dependency(AbilitySystemComponent, DependencyLevel::PreDeclaration);
                    header_data.append_line(fmt::format(STR("virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override {}"),
                                                        generate_virtual_body(STR("GetAbilitySystemComponent"), STR("return nullptr;"), is_abstract)));
                }
                static const auto NAME_Interface_PostProcessVolume = FName(STR("Interface_PostProcessVolume"), FNAME_Add);
                if (name == NAME_Interface_PostProcessVolume)
                {
                    // FVector is in CoreMinimal.h
                    header_data.append_line(fmt::format(STR("virtual bool EncompassesPoint(FVector Point, float SphereRadius/*=0.f*/, float* OutDistanceToPoint) override {}"), generate_virtual_body(STR("EncompassesPoint"), STR("return false;"), is_abstract)));
                    // FPostProcessVolumeProperties is declared in Interface_PostProcessVolume.h and not reflected
                    header_data.append_line(fmt::format(STR("virtual FPostProcessVolumeProperties GetProperties() const override {}"),
                                                        generate_virtual_body(STR("GetProperties"), STR("return {};"), is_abstract)));
                }
                static const auto NAME_BlendableInterface = FName(STR("BlendableInterface"), FNAME_Add);
                if (name == NAME_BlendableInterface)
                {
                    header_data.append_line(fmt::format(STR("virtual void OverrideBlendableSettings(class FSceneView& View, float Weight) const override {}"),
                                                        generate_virtual_body(STR("OverrideBlendableSettings"), STR(""), is_abstract)));
                }
            }
        }
        for (auto klass = uclass; klass; klass = klass->GetSuperClass())
        {
            auto name = klass->GetNamePrivate();
            static const auto NAME_TickableWorldSubsystem = FName(STR("TickableWorldSubsystem"), FNAME_Add);
            if (name == NAME_TickableWorldSubsystem)
            {
                header_data.append_line(fmt::format(STR("virtual TStatId GetStatId() const override {}"),
                                                    generate_virtual_body(STR("GetStatId"), STR("return {};"), is_abstract)));
            }
        }

        header_data.end_indent_level();
        header_data.append_line(STR("};"));
    }

    auto UEHeaderGenerator::generate_struct_definition(UScriptStruct* script_struct, GeneratedSourceFile& header_data) -> void
    {
        const std::wstring struct_native_name = get_native_struct_name(script_struct);
        const std::wstring struct_flags_string = generate_struct_flags(script_struct);

        auto is_struct_exported = script_struct->HasAnyStructFlags(STRUCT_RequiredAPI);
        StringType api_macro_name = convert_module_name_to_api_name(get_module_name_for_package(header_data.get_package()));
        api_macro_name += ' ';

        UScriptStruct* super_struct = script_struct->GetSuperScriptStruct();
        StringType parent_struct_declaration;
        if (super_struct)
        {
            header_data.add_dependency(super_struct, DependencyLevel::Include);

            auto super_struct_native_name = get_native_struct_name(super_struct);
            parent_struct_declaration = fmt::format(STR(" : public {}"), super_struct_native_name);
        }

        header_data.format_line(STR("USTRUCT({})"), struct_flags_string);
        header_data.format_line(STR("struct {}{}{} {{"), is_struct_exported ? api_macro_name : STR(""), struct_native_name, parent_struct_declaration);
        header_data.begin_indent_level();

        header_data.append_line(STR("GENERATED_BODY()"));

        AccessModifier current_access_modifier = AccessModifier::None;
        append_access_modifier(header_data, AccessModifier::Public, current_access_modifier);

        // Generate struct properties
        for (FProperty* property : script_struct->ForEachProperty())
        {
            append_access_modifier(header_data, get_property_access_modifier(property), current_access_modifier);
            generate_property(script_struct, property, header_data);
        }

        // Generate constructor and make sure it's public
        append_access_modifier(header_data, AccessModifier::Public, current_access_modifier);
        header_data.format_line(STR("{}{}();"), !is_struct_exported ? api_macro_name : STR(""), struct_native_name);

        header_data.end_indent_level();
        header_data.append_line(STR("};"));

        if (m_structs_that_need_get_type_hash.contains(script_struct))
        {
            header_data.format_line(STR("FORCEINLINE uint32 GetTypeHash(const {}&) {{ return 0; }}"), struct_native_name);
            header_data.format_line(STR("FORCEINLINE bool operator==(const {}&, const {}&) {{ return true; }}"), struct_native_name, struct_native_name);
        }
        static FName NAME_TickFunction{STR("TickFunction")};
        if (super_struct && super_struct->GetNamePrivate() == NAME_TickFunction)
        {
            // TODO: I don't really know what this was for; it was added to 2nd pass as a special case but never used any information from the first pass
            header_data.append_line();
            header_data.append_line(STR("template<>"));
            header_data.format_line(STR("struct TStructOpsTypeTraits<{}> : public TStructOpsTypeTraitsBase2<{}>"), struct_native_name, struct_native_name);
            header_data.append_line(STR("{"));
            header_data.begin_indent_level();
            header_data.append_line(STR("enum"));
            header_data.append_line(STR("{"));
            header_data.begin_indent_level();
            header_data.append_line(STR("WithCopy = false"));
            header_data.end_indent_level();
            header_data.append_line(STR("};"));
            header_data.end_indent_level();
            header_data.append_line(STR("};"));
        }
    }

    auto UEHeaderGenerator::generate_enum_definition(UEnum* uenum, GeneratedSourceFile& header_data) -> void
    {
        const StringType native_enum_name = get_native_enum_name(uenum, false);
        const int64 highest_enum_value = get_highest_enum(uenum);
        const bool can_use_uint8_override = get_lowest_enum(uenum) >= 0 && highest_enum_value <= 255;
        const StringType enum_flags_string = generate_enum_flags(uenum);
        const auto underlying_prop = m_underlying_enum_props.find(uenum);
        const bool has_known_underlying_prop = underlying_prop != m_underlying_enum_props.end();
        UEnum::ECppForm cpp_form = uenum->GetCppForm();
        bool enum_is_uint8{false};

        header_data.append_line(fmt::format(STR("UENUM({})"), enum_flags_string));

        if (cpp_form == UEnum::ECppForm::Namespaced)
        {
            header_data.append_line(fmt::format(STR("namespace {} {{"), native_enum_name));
            header_data.begin_indent_level();
            header_data.append_line(STR("enum Type {"));
        }
        else if (cpp_form == UEnum::ECppForm::Regular)
        {
            header_data.append_line(fmt::format(STR("enum {} {{"), native_enum_name));
        }
        else if (cpp_form == UEnum::ECppForm::EnumClass)
        {
            if (!has_known_underlying_prop)
            {
                if (UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeEnumClassesBlueprintType && can_use_uint8_override)
                {
                    header_data.append_line(fmt::format(STR("enum class {} : uint8 {{"), native_enum_name));
                    enum_is_uint8 = true;
                }
                else
                {
                    // Enum has never been used in any native classes or structures, go with implicit type
                    header_data.append_line(fmt::format(STR("enum class {} {{"), native_enum_name));
                }
            }
            else
            {
                PropertyTypeDeclarationContext context(native_enum_name, &header_data);
                std::wstring underlying_prop_string = generate_property_type_declaration(underlying_prop->second, context);

                header_data.append_line(fmt::format(STR("enum class {} : {} {{"), native_enum_name, underlying_prop_string));
            }
        }

        header_data.begin_indent_level();

        StringType enum_prefix = uenum->GenerateEnumPrefix();
        int64 expected_next_enum_value = 0;
        bool last_value_was_negative_one{false};
        std::set<StringType> enum_name_set{};
        for (auto [Name, Value] : uenum->ForEachName())
        {
            StringType enum_name = Name.ToString();
            StringType result_enumeration_line = sanitize_enumeration_name(enum_name);
            StringType pre_append_result_line = result_enumeration_line;

            // If an enum name is listed in the array twice, that likely means it is used as the value for another enum.  Long story short, don't print it.
            if (enum_name_set.contains(enum_name))
            {
                continue;
            }
            else
            {
                enum_name_set.emplace(enum_name);
            }

            // Taking advantage of GetNameByValue returning the first result for the value to determine if there are any enumerator names that
            // reference an already declared value/name.
            StringType first_name_with_value = uenum->GetNameByValue(Value).ToString();
            if (first_name_with_value != Name.ToString())
            {
                result_enumeration_line.append(fmt::format(STR(" = {}"), sanitize_enumeration_name(first_name_with_value)));
            }
            else if (Value != expected_next_enum_value || last_value_was_negative_one)
            {
                const StringType CastString = (enum_is_uint8 && Value < 0) ? STR("(uint8)") : STR("");
                const StringType MinusSign = Value < 0 ? STR("-") : STR("");
                result_enumeration_line.append(fmt::format(STR(" = {}{}{}"), CastString, MinusSign, std::abs(Value)));
            }
            expected_next_enum_value = Value + 1;
            last_value_was_negative_one = (Value == -1);

            StringType pre_append_result_line_lower = pre_append_result_line;
            std::transform(pre_append_result_line_lower.begin(), pre_append_result_line_lower.end(), pre_append_result_line_lower.begin(), ::towlower);
            if (pre_append_result_line_lower.ends_with(STR("_max")))
            {
                const StringType expected_full_constant_name = fmt::format(STR("{}_MAX"), enum_prefix);
                StringType expected_full_constant_name_lower = expected_full_constant_name;
                std::transform(expected_full_constant_name_lower.begin(), expected_full_constant_name_lower.end(), expected_full_constant_name_lower.begin(), ::towlower);

                int64_t expected_max_value = highest_enum_value + 1;

                // Skip enum _MAX constant if it has a matching name and is 1 greater than the highest value used, which means it has been autogenerated
                if ((pre_append_result_line_lower == expected_full_constant_name_lower ||
                     pre_append_result_line_lower == sanitize_enumeration_name(expected_full_constant_name_lower)) &&
                    Value == expected_max_value)
                {
                    continue;
                }
                // Otherwise, just make sure it's hidden and not visible to the end user
                result_enumeration_line.append(STR(" UMETA(Hidden)"));
            }

            result_enumeration_line.append(STR(","));
            header_data.append_line(result_enumeration_line);
        }

        header_data.end_indent_level();
        header_data.append_line(STR("};"));

        if (cpp_form == UEnum::ECppForm::Namespaced)
        {
            header_data.end_indent_level();
            header_data.append_line(STR("}"));
        }
    }

    auto UEHeaderGenerator::generate_delegate_type_declaration(UFunction* signature_function, UClass* delegate_class, GeneratedSourceFile& header_data) -> void
    {
        std::wstring owning_class;
        if (delegate_class == nullptr)
        {
            owning_class = STR("UObject*");
        }
        else
        {
            owning_class = delegate_class->GetNamePrivate().ToString();
        }

        auto function_flags = signature_function->GetFunctionFlags();
        assert(function_flags & Unreal::FUNC_Delegate);

        // TODO not particularly nice or reliable, but will do for now
        const bool is_sparse = signature_function->GetClassPrivate()->GetName() == STR("SparseDelegateFunction");

        const bool is_multicast = (function_flags & Unreal::FUNC_MulticastDelegate) != 0;
        const bool declared_const = (function_flags & FUNC_Const) != 0;

        auto delegate_type_name = get_native_delegate_type_name(signature_function, nullptr, true);
        auto return_value_property = signature_function->GetReturnProperty();

        // Delegate macro declaration is only allowed on the top level delegates, class-based types are limited to being implicit
        if (signature_function->GetOuterPrivate()->IsA<UPackage>())
        {
            header_data.format_line(STR("UDELEGATE({})"), generate_function_flags(signature_function));
        }

        PropertyTypeDeclarationContext context(delegate_type_name, &header_data, false, true);

        int32_t num_delegate_parameters = 0;
        std::wstring delegate_parameter_list =
                generate_function_parameter_list(nullptr, signature_function, header_data, true, context.context_name, {}, &num_delegate_parameters);
        if (num_delegate_parameters > 0)
        {
            delegate_parameter_list.insert(0, STR(", "));
        }

        if (num_delegate_parameters > 9)
        {
            Output::send<LogLevel::Error>(STR("Invalid delegate parameter count in Delegate: {}. Using _TooMany\n"), delegate_type_name);
        }

        std::wstring return_value_declaration;
        if (return_value_property != NULL)
        {
            return_value_declaration = generate_property_type_declaration(return_value_property, context);
            return_value_declaration.append(STR(", "));
        }

        header_data.format_line(STR("DECLARE_DYNAMIC{}{}_DELEGATE{}{}{}({}{}{}{});"),
                                is_multicast ? STR("_MULTICAST") : STR(""),
                                is_sparse ? STR("_SPARSE") : STR(""),
                                return_value_property ? STR("_RetVal") : STR(""),
                                generate_parameter_count_string(num_delegate_parameters),
                                declared_const ? STR("_Const") : STR(""),
                                return_value_declaration,
                                delegate_type_name,
                                // TODO: Actually get delegate property name.
                                is_sparse ? fmt::format(STR("{}, {}"), owning_class, STR("EnterPropertyName")) : STR(""),
                                delegate_parameter_list);
    }

    auto UEHeaderGenerator::generate_object_implementation(UClass* uclass, GeneratedSourceFile& implementation_file) -> void
    {
        auto class_native_name = get_native_class_name(uclass);

        auto constructor_args = STR("");
        auto constructor_postfix_string = STR("");
        auto super_class = uclass->GetSuperClass();
        auto native_parent_class_name = super_class ? get_native_class_name(super_class) : STR("UObjectBaseUtility");

        // Generate constructor implementation

        // If class is a child of AActor we add the UObjectInitializer constructor.
        // This may not be required in all cases, but is necessary to override subcomponents and does not hurt anything.
        if (should_generate_fobjectinitializer_constructor(uclass))
        {
            constructor_args = STR("const FObjectInitializer& ObjectInitializer");
            constructor_postfix_string = STR(") : Super(ObjectInitializer");
        }
        // If parent class contains the UObjectInitializer constructor without default value,
        // we need to create the explicit call to such constructor and pass UObjectInitializer::Get() as the argument.
        else if (needs_fobjectinitializer_constructor(uclass->GetSuperClass()))
        {
            constructor_postfix_string = STR(") : Super(FObjectInitializer::Get()");
        }

        StringType constructor = fmt::format(STR("{}::{}({}{}"), class_native_name, class_native_name, constructor_args, constructor_postfix_string);

        auto constructor_pos = implementation_file.current_position();
        implementation_file.begin_indent_level();

        // subobject management
        auto const& dsos = m_default_subobjects[uclass];
        DefaultSubobjects empty_dsos;
        auto const& super_dsos = super_class ? m_default_subobjects[super_class] : empty_dsos;
        for (auto [dso_name, dso] : dsos)
        {
            if (!dso)
            {
                fmt::format_to(std::back_inserter(constructor), STR(".DoNotCreateDefaultSubobject(TEXT(\"{}\"))"), dso_name.ToString());
                continue;
            }
            auto dso_class = dso->GetClassPrivate();
            auto super = super_dsos.find(dso_name);
            if (!super.second)
            {
                auto id = implementation_file.gen_id();
                implementation_file.dso_lookup.insert({dso, id});
                implementation_file.add_dependency(dso_class, DependencyLevel::Include);
                implementation_file.format_line(STR("auto gen{} = CreateDefaultSubobject<{}>(TEXT(\"{}\"));"), id, get_native_class_name(dso_class), dso_name.ToString());
                continue;
            }
            if (!super.first || dso_class != super.first->GetClassPrivate())
            {
                implementation_file.add_dependency(dso_class, DependencyLevel::Include);
                fmt::format_to(std::back_inserter(constructor), STR(".SetDefaultSubobjectClass<{}>(TEXT(\"{}\"))"), get_native_class_name(dso_class), dso_name.ToString());
            }
        }
        constructor.append(STR(") {\n"));
        implementation_file.insert(constructor_pos, constructor);

        // Generate and initialize properties
        auto cdo = uclass->GetClassDefaultObject();
        PropertyScope scope{};
        for (FProperty* property : uclass->ForEachProperty())
        {
            generate_property_assignment_in_container(uclass, property, cdo, nullptr, implementation_file, scope, false);
        }

        auto super_cdo = super_class ? super_class->GetClassDefaultObject() : nullptr;
        for (; super_class; super_class = super_class->GetSuperClass())
        {
            for (FProperty* property : super_class->ForEachProperty())
            {
                generate_property_assignment_in_container(uclass, property, cdo, super_cdo, implementation_file, scope, false);
            }
        }
        m_class_subobjects.clear();

        // set up attachments
        static auto attach_prop = CastField<FObjectProperty>(USceneComponent::StaticClass()->GetPropertyByName(STR("AttachParent")));
        for (auto [dso_name, dso] : dsos)
        {
            if (!dso || !dso->IsA<USceneComponent>()) continue;
            if (super_dsos.find(dso_name).second)
            {
                // parent should have handled attachment and we cannot
                continue;
            }

            auto parent = attach_prop->GetPropertyValueInContainer(dso);
            if (!parent)
            {
                // TODO: we directly set RootComponent in properties above; we should use SetRootComponent instead.
                continue;
            }
            auto child_value = generate_dso_value(uclass, dso, implementation_file);
            auto parent_value = generate_dso_value(uclass, parent, implementation_file);
            implementation_file.format_line(STR("if ({}) {}->SetupAttachment({});"), child_value, child_value, parent_value);
        }

        implementation_file.end_indent_level();
        implementation_file.append_line(STR("}"));
        implementation_file.append_line();

        CaseInsensitiveSet blacklisted_parameter_names{};

        // Generate functions
        for (UFunction* function : uclass->ForEachFunction())
        {
            if (!is_delegate_signature_function(function))
            {
                generate_function_implementation(uclass, function, implementation_file, false, blacklisted_parameter_names);
                implementation_file.append_line();
            }
        }

        bool encountered_replicated_properties = false;

        for (FProperty* property : uclass->ForEachProperty())
        {
            encountered_replicated_properties |= (property->GetPropertyFlags() & CPF_Net) != 0;
        }

        // Generate replicated properties implementation if we really need it
        if (encountered_replicated_properties)
        {
            implementation_file.add_extra_include(STR("Net/UnrealNetwork.h"));

            implementation_file.append_line(
                    fmt::format(STR("void {}::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {{"), class_native_name));
            implementation_file.begin_indent_level();

            implementation_file.append_line(STR("Super::GetLifetimeReplicatedProps(OutLifetimeProps);"));
            implementation_file.append_line();

            for (FProperty* property : uclass->ForEachProperty())
            {
                if ((property->GetPropertyFlags() & CPF_Net) != 0)
                {
                    implementation_file.append_line(fmt::format(STR("DOREPLIFETIME({}, {});"), class_native_name, fix_cpp_keyword_name(property->GetName())));
                }
            }

            implementation_file.end_indent_level();
            implementation_file.append_line(STR("}"));
            implementation_file.append_line();
        }
    }

    auto UEHeaderGenerator::generate_struct_implementation(UScriptStruct* this_struct, GeneratedSourceFile& implementation_file) -> void
    {
        auto struct_native_name = get_native_struct_name(this_struct);

        // Generate constructor implementation and initialize properties inside
        implementation_file.format_line(STR("{}::{}() {{"), struct_native_name, struct_native_name);
        implementation_file.begin_indent_level();

        // Generate properties
        PropertyScope scope{};
        auto struct_defaults = get_default_object(this_struct);
        for (FProperty* property : this_struct->ForEachProperty())
        {
            generate_property_assignment_in_container(this_struct, property, struct_defaults, nullptr, implementation_file, scope, true);
        }
        auto super = this_struct->GetSuperStruct();
        auto super_defaults = super ? get_default_object(super) : nullptr;
        for (; super; super = super->GetSuperStruct())
        {
            for (FProperty* property : super->ForEachProperty())
            {
                generate_property_assignment_in_container(this_struct, property, struct_defaults, super_defaults, implementation_file, scope, false);
            }
        }

        implementation_file.end_indent_level();
        implementation_file.append_line(STR("}"));
    }

    auto UEHeaderGenerator::generate_property(UStruct* ustruct, FProperty* property, GeneratedSourceFile& header_data) -> void
    {
        auto property_flags = generate_property_flags(ustruct, property);

        bool is_bitmask_bool = false;
        PropertyTypeDeclarationContext Context(ustruct->GetName(), &header_data, true, false, &is_bitmask_bool);

        auto property_decl = generate_property_type_declaration(property, Context);
        property_decl.append(STR(" "));
        write_property_name(property_decl, property);
        if (property->GetArrayDim() != 1)
        {
            fmt::format_to(std::back_inserter(property_decl), STR("[{}]"), property->GetArrayDim());
        }
        else if (is_bitmask_bool)
        {
            property_decl.append(STR(": 1"));
        }
        property_decl.append(STR(";"));

        header_data.append_line(fmt::format(STR("UPROPERTY({})"), property_flags));
        header_data.append_line(property_decl);
        header_data.append_line();
    }

    // TODO FUNC_Final is not properly handled (should be always set except some weird cases)
    auto UEHeaderGenerator::generate_function(UClass* uclass,
                                              UFunction* function,
                                              GeneratedSourceFile& header_data,
                                              bool is_generating_interface,
                                              CaseInsensitiveSet const& blacklisted_parameter_names,
                                              bool generate_as_override) -> void
    {
        auto function_flags = function->GetFunctionFlags();
        const std::wstring context_name = uclass->GetName();
        bool is_function_pure_virtual = generate_as_override;

        std::wstring function_modifier_string;
        if ((function_flags & FUNC_Static) != 0)
        {
            function_modifier_string.append(STR("static "));
        }
        else if ((function_flags & FUNC_BlueprintEvent) == 0 && is_generating_interface)
        {
            // When we have a blueprint function that is not blueprint event inside the interface,
            // it means we are dealing with the native interface that cannot be implemented via blueprints
            // and uses pure virtual functions implemented through native code
            function_modifier_string.append(STR("virtual "));
            is_function_pure_virtual = true;
        }

        FProperty* return_property = function->GetReturnProperty();
        std::wstring return_property_string;
        if (return_property != NULL)
        {
            PropertyTypeDeclarationContext context(uclass->GetName(), &header_data, false, true);
            return_property_string = generate_property_type_declaration(return_property, context);
        }
        else
        {
            return_property_string = STR("void");
        }

        std::wstring function_extra_postfix_string;
        if ((function_flags & FUNC_Const) != 0)
        {
            function_extra_postfix_string.append(STR(" const"));
        }
        if (is_function_pure_virtual)
        {
            std::wstring return_statement_string;
            if (return_property != NULL)
            {
                auto default_value = generate_default_property_value(uclass, return_property, header_data);
                return_statement_string = fmt::format(STR(" return {};"), default_value);
            }

            if (generate_as_override)
            {
                function_extra_postfix_string.append(STR(" override"));
            }
            function_extra_postfix_string.append(fmt::format(STR(" PURE_VIRTUAL({},{})"), function->GetName(), return_statement_string));
        }

        std::wstring function_argument_list = generate_function_parameter_list(uclass, function, header_data, false, context_name, blacklisted_parameter_names);

        const std::wstring function_flags_string = generate_function_flags(function, is_function_pure_virtual);
        header_data.append_line(fmt::format(STR("UFUNCTION({})"), function_flags_string));
        // Format for virtual functions
        // virtual <return type> <function_name>(<params>) PURE_VIRTUAL(<function name>, <return statement>)
        header_data.append_line(fmt::format(STR("{}{} {}({}){};"),
                                            function_modifier_string,
                                            return_property_string,
                                            function->GetName(),
                                            function_argument_list,
                                            function_extra_postfix_string));
        header_data.append_line();
    }

    static FProperty* get_property_by_fname_in_chain(UStruct* ustruct, FName name)
    {
        for (; ustruct; ustruct = ustruct->GetSuperStruct())
        {
            for (FProperty* prop : ustruct->ForEachProperty())
            {
                if (prop->GetFName() == name)
                {
                    return prop;
                }
            }
        }
        return nullptr;
    }
    auto UEHeaderGenerator::needs_advanced_access(UStruct* this_struct, FProperty* prop) -> bool
    {
        bool is_private = prop->GetPropertyFlags() & CPF_NativeAccessSpecifierPrivate;
        bool is_protected = prop->GetPropertyFlags() & CPF_NativeAccessSpecifierProtected;
        UStruct* outer = Cast<UStruct>(prop->GetOutermostOwner());
        return is_private && outer != this_struct || is_protected && !this_struct->IsChildOf(outer);
    }
    auto UEHeaderGenerator::is_default_value(FProperty* property, void const* data, void const* archetype_data)
        -> bool
    {
        if (archetype_data)
        {
            return property->Identical(data, archetype_data);
        }

        if (auto prop = CastField<FBoolProperty>(property))
        {
            uint64_t zero = 0ULL;
            return prop->Identical(data, &zero);
        }
        if (auto prop = CastField<FStructProperty>(property))
        {
            auto struct_defaults = get_default_object(prop->GetStruct());
            return prop->Identical(data, struct_defaults);
        }
        if (auto prop = CastField<FTextProperty>(property))
        {
            auto& value = prop->GetPropertyValue(data);
            Output::send<LogLevel::Warning>(STR("FTextProperty is_default_value is incorrectly implemented in {}\n"), property->GetFullName());
            return !value.Data || value.ToFString().Len() == 0;
        }
        if (property->IsA<FArrayProperty>())
        {
            auto& value = *static_cast<FScriptArray const*>(data);
            return value.Num() == 0;
        }
        if (property->IsA<FSetProperty>() || property->IsA<FMapProperty>())
        {
            auto& value = *static_cast<FScriptSet const*>(data);
            return value.Num() == 0;
        }
        if (property->IsA<FSoftObjectProperty>())
        {
            auto& value = *static_cast<FSoftObjectPath const*>(data);
            return value.AssetPathName == NAME_None;
        }
        if (auto prop = CastField<FMulticastSparseDelegateProperty>(property))
        {
            auto& value = prop->GetPropertyValue(data);
            return !value.b_is_bound;
        }
        if (!property->HasAnyPropertyFlags(CPF_ZeroConstructor))
        {
            Output::send<LogLevel::Warning>(STR("Incorrectly implemented is_default_value for {} (non-zero constructor)\n"), property->GetFullName());
            return false;
        }
        char alignas(8) default_value[40] = {}; // FSoftObjectPtr
        if (sizeof(default_value) < property->GetElementSize())
        {
            Output::send<LogLevel::Warning>(STR("Buffer insufficient in is_default_value for {} (size = {})\n"), property->GetFullName(), property->GetElementSize());
            return false;
        }
        return property->Identical(data, default_value);
    }
    auto UEHeaderGenerator::get_default_object(UStruct* ustruct) -> void const*
    {
        if (auto uclass = Cast<UClass>(ustruct))
        {
            return uclass->GetClassDefaultObject();
        }
        auto& d = m_struct_defaults[ustruct];
        if (!d)
        {
            d = ALIGNED_ALLOC(ustruct->GetMinAlignment(), ustruct->GetStructureSize());
            memset(d, 0, ustruct->GetStructureSize());
            // TODO: BuildConstantLookupTable (e.g. FFloatDistribution) wants to run in game thread or async loading thread, which we are not
            ustruct->InitializeStruct(d);
        }
        return d;
    }

    UEHeaderGenerator::~UEHeaderGenerator()
    {
        for (auto [ustruct, d] : m_struct_defaults)
        {
            ustruct->DestroyStruct(d);
            ALIGNED_FREE(d);
        }
    }

    auto UEHeaderGenerator::generate_enum_value(UEnum* uenum, int64_t enum_value, GeneratedSourceFile& implementation_file) -> std::wstring
    {
        implementation_file.add_dependency(uenum, DependencyLevel::Include);

        UEnum::ECppForm cpp_form = uenum->GetCppForm();
        const std::wstring enum_native_name = get_native_enum_name(uenum, false);

        std::wstring enum_constant_name;
        for (auto [Name, Value] : uenum->ForEachName())
        {
            if (Value == enum_value)
            {
                enum_constant_name = sanitize_enumeration_name(Name.ToString());
            }
        }
        if (enum_constant_name.empty())
        {
            Output::send<LogLevel::Warning>(STR("Invalid value for enum '{}', casting instead of using enum name. Value '{}' will be cast to the enum.\n"),
                         enum_native_name,
                         enum_value);
            return fmt::format(STR("({}){}"), enum_native_name, enum_value);
        }
        else
        {
            // Regular enumerations do not really need an enum name prefix
            if (cpp_form == UEnum::ECppForm::Regular)
            {
                return enum_constant_name;
            }
            return fmt::format(STR("{}::{}"), enum_native_name, enum_constant_name);
        }
    }
    auto UEHeaderGenerator::generate_dso_value(UStruct* this_struct, UObject* object, GeneratedSourceFile& implementation_file) -> StringType
    {
        if (object->GetOuterPrivate()->GetClassPrivate() != this_struct)
        {
            Output::send<LogLevel::Error>(STR("nested default subobjects are unimplemented: {}\n"), object->GetFullName());
            return STR("nullptr");
        }
        auto& id = implementation_file.dso_lookup[object];
        if (id == 0)
        {
            // we eagerly created new subobjects, so this should be a subobject from a parent class
            id = implementation_file.gen_id();
            auto uclass = object->GetClassPrivate();
            while (!uclass->HasAnyClassFlags(CLASS_Native))
            {
                uclass = uclass->GetSuperClass();
            }
            auto class_name = get_native_class_name(uclass);
            implementation_file.add_dependency(uclass, DependencyLevel::Include);
            implementation_file.format_line(STR("auto gen{} = Cast<{}>(GetDefaultSubobjectByName(TEXT(\"{}\")));"), id, class_name, object->GetName());
        }
        return fmt::format(STR("gen{}"), id);
    }

    auto UEHeaderGenerator::generate_soft_path(std::wstring_view kind, FSoftObjectPath const& value) -> std::wstring
    {
        return value.AssetPathName == NAME_None
            ? fmt::format(STR("{}()"), kind)
            : fmt::format(STR("{}(TEXT(\"{}\"), TEXT(\"\"))"), kind, value.AssetPathName.ToString(), value.SubPathString.GetCharArray());
    }
    auto UEHeaderGenerator::generate_object_finder(UClass* class_, std::wstring_view path_name, GeneratedSourceFile& implementation_file, bool is_class) -> std::wstring
    {
        implementation_file.add_dependency(class_, DependencyLevel::Include);
        auto finder_id = implementation_file.gen_id();
        // XXX: shouldn't really be optional
        // TODO: class should generate FClassFinder
        implementation_file.append_line(fmt::format(STR("static ConstructorHelpers::{}<{}> gen{}(TEXT(\"{}\"));"),
                                                    is_class ? STR("FClassFinder") : STR("FObjectFinder"),
                                                    get_native_class_name(class_),
                                                    finder_id,
                                                    path_name));
        return fmt::format(STR("gen{}.{}"), finder_id, is_class ? STR("Class") : STR("Object"));
    }
    auto UEHeaderGenerator::generate_property_assignment_in_container(UStruct* this_struct, FProperty* property, void const* object, void const* archetype, GeneratedSourceFile& implementation_file, PropertyScope& property_scope, bool write_defaults) -> void
    {
        auto data = property->ContainerPtrToValuePtr<void>(object);
        auto arch_data = archetype ? property->ContainerPtrToValuePtr<void>(archetype) : nullptr;
        generate_property_assignment(this_struct, property, data, arch_data, implementation_file, property_scope, write_defaults);
    }
    auto UEHeaderGenerator::generate_property_assignment(UStruct* this_struct, FProperty* property, void const* data, void const* arch_data, GeneratedSourceFile& implementation_file, PropertyScope& property_scope, bool write_defaults) -> void
    {
        static FName NAME_NativeClass{STR("NativeClass"), FNAME_Add};
        FName property_name = property->GetFName();
        if (property_name == NAME_NativeClass || ignore_default.includes(property)) return;

        auto it = property_element_setters().find(property);
        auto setter = it != property_element_setters().end() ? it->second : &UEHeaderGenerator::generate_property_element_assignment;

        auto ptr = static_cast<char const*>(data);
        auto arch_ptr = static_cast<char const*>(arch_data);
        for (int32 index = 0; index < property->GetArrayDim(); ++index)
        {
            if (!write_defaults && is_default_value(property, ptr, arch_ptr)) continue;

            property_scope.push(property, index);
            (this->*setter)(this_struct, property, ptr, arch_ptr, implementation_file, property_scope, write_defaults);
            property_scope.pop();

            ptr += property->GetElementSize();
            if (arch_ptr) arch_ptr += property->GetElementSize();
        }
    }
    auto UEHeaderGenerator::generate_property_element_assignment(UStruct* this_struct, FProperty* property, void const* data, void const* arch_data, GeneratedSourceFile& implementation_file, PropertyScope& property_scope, bool write_defaults) -> void
    {
        if (auto prop = CastField<FObjectProperty>(property))
        {
            auto value = prop->GetPropertyValue(data);
            auto arch_value = arch_data ? prop->GetPropertyValue(arch_data) : nullptr;
            if (value && arch_value && value->HasAnyFlags(RF_DefaultSubObject) && arch_value->HasAnyFlags(RF_DefaultSubObject) && value->GetNamePrivate() == arch_value->GetNamePrivate())
            {
                // skip writing DSO
                // TODO: this check should be done structurally recursively in is_default_value
                return;
            }
        }
        if (auto prop = CastField<FStructProperty>(property))
        {
            auto script_struct = prop->GetStruct();
            if (!struct_generators().contains(script_struct->GetNamePrivate()))
            {
                for (auto st = script_struct; st; st = st->GetSuperScriptStruct())
                {
                    for (FProperty* child_prop : st->ForEachProperty())
                    {
                        generate_property_assignment_in_container(this_struct, child_prop, data, arch_data, implementation_file, property_scope, write_defaults);
                    }
                }
                return;
            }
        }
        // if (auto prop = CastField<FArrayProperty>(property))
        //{
        // }
        // if (auto prop = CastField<FSetProperty>(property))
        //{
        // }
        // if (auto prop = CastField<FMapProperty>(property))
        //{
        // }

        auto value = generate_property_element_value(this_struct, property, data, implementation_file);
        auto access = property_scope.access(this_struct, implementation_file);
        if (auto prop = CastField<FBoolProperty>(property))
        {
            if (prop->GetFieldMask() != 0xff && needs_advanced_access(this_struct, prop))
            {
                auto prop_str = implementation_file.generate_cons_property(prop);
                implementation_file.format_line(STR("CastField<FBoolProperty>({})->SetPropertyValue(&{}, {});"), prop_str, access, value);
                return;
            }
        }
        implementation_file.format_line(STR("{} = {};"), access, value);
    }

    auto UEHeaderGenerator::generate_function_implementation(UClass* uclass,
                                                             UFunction* function,
                                                             GeneratedSourceFile& implementation_file,
                                                             bool is_generating_interface,
                                                             const CaseInsensitiveSet& blacklisted_parameter_names) -> void
    {
        const std::wstring class_native_name = get_native_class_name(uclass, is_generating_interface);
        const std::wstring raw_function_name = function->GetName();
        auto function_flags = function->GetFunctionFlags();
        PropertyTypeDeclarationContext context(uclass->GetName(), &implementation_file, false, true);

        std::wstring function_implementation_name;
        std::wstring net_validate_function_name;
        bool is_input_function_const = ((function_flags)&FUNC_Const) != 0;

        if ((function_flags & FUNC_Net) != 0)
        {
            // Network functions always have the implementation inside the _Implementation function
            function_implementation_name = fmt::format(STR("{}::{}_Implementation"), class_native_name, raw_function_name);

            // Validated network functions by default have their validation function name set to _Validate
            if ((function_flags & FUNC_NetValidate) != 0)
            {
                net_validate_function_name = fmt::format(STR("{}::{}_Validate"), class_native_name, raw_function_name);
            }
        }
        else if ((function_flags & FUNC_BlueprintEvent) != 0)
        {
            // Blueprint Events use _Implementation by default too, but only BlueprintNativeEvents.
            // BlueprintImplementableEvents do not have any native functions at all, they're just thunks
            if ((function_flags & FUNC_Native) != 0)
            {
                function_implementation_name = fmt::format(STR("{}::{}_Implementation"), class_native_name, raw_function_name);
            }
        }
        else
        {
            // Otherwise, normal UFunctions get a standard name matching the function in question
            function_implementation_name = fmt::format(STR("{}::{}"), class_native_name, raw_function_name);
        }

        std::wstring function_parameter_list;
        if (!function_implementation_name.empty() || !net_validate_function_name.empty())
        {
            function_parameter_list =
                    generate_function_parameter_list(uclass, function, implementation_file, false, context.context_name, blacklisted_parameter_names);
        }

        if (!function_implementation_name.empty())
        {
            FProperty* return_value_property = function->GetReturnProperty();

            const std::wstring return_value_type = return_value_property ? generate_property_type_declaration(return_value_property, context) : STR("void");

            implementation_file.append_line(fmt::format(STR("{} {}({}){} {{"),
                                                        return_value_type,
                                                        function_implementation_name,
                                                        function_parameter_list,
                                                        is_input_function_const ? STR(" const") : STR("")));
            implementation_file.begin_indent_level();

            if (return_value_property != NULL)
            {
                auto default_value = generate_default_property_value(uclass, return_value_property, implementation_file);
                implementation_file.append_line(fmt::format(STR("return {};"), default_value));
            }

            implementation_file.end_indent_level();
            implementation_file.append_line(STR("}"));
        }

        if (!net_validate_function_name.empty())
        {
            implementation_file.append_line(fmt::format(STR("bool {}({}) {{"), net_validate_function_name, function_parameter_list));
            implementation_file.begin_indent_level();

            implementation_file.append_line(STR("return true;"));

            implementation_file.end_indent_level();
            implementation_file.append_line(STR("}"));
        }
    }

    auto UEHeaderGenerator::generate_parameter_count_string(int32_t parameter_count) -> CharType const*
    {
        switch (parameter_count)
        {
        case 0:
            return STR("");
        case 1:
            return STR("_OneParam");
        case 2:
            return STR("_TwoParams");
        case 3:
            return STR("_ThreeParams");
        case 4:
            return STR("_FourParams");
        case 5:
            return STR("_FiveParams");
        case 6:
            return STR("_SixParams");
        case 7:
            return STR("_SevenParams");
        case 8:
            return STR("_EightParams");
        case 9:
            return STR("_NineParams");
        default:
            return STR("_TooMany");
        }
    }

    auto UEHeaderGenerator::append_access_modifier(GeneratedSourceFile& header_data, AccessModifier needed_access, AccessModifier& current_access) -> void
    {
        if (current_access != needed_access)
        {
            current_access = needed_access;

            if (needed_access == AccessModifier::Public)
            {
                header_data.append_line_no_indent(STR("public:"));
            }
            else if (needed_access == AccessModifier::Protected)
            {
                header_data.append_line_no_indent(STR("protected:"));
            }
            else if (needed_access == AccessModifier::Private)
            {
                header_data.append_line_no_indent(STR("private:"));
            }
        }
    }

    auto UEHeaderGenerator::get_function_access_modifier(UFunction* function) -> AccessModifier
    {
        auto function_flags = function->GetFunctionFlags();

        if ((function_flags & FUNC_Private) != 0)
        {
            return AccessModifier::Private;
        }
        else if ((function_flags & FUNC_Protected) != 0)
        {
            return AccessModifier::Protected;
        }
        else if ((function_flags & FUNC_Public) != 0)
        {
            return AccessModifier::Public;
        }
        return AccessModifier::Public;
    }

    auto UEHeaderGenerator::get_property_access_modifier(FProperty* property) -> AccessModifier
    {
        auto property_flags = property->GetPropertyFlags();

        if ((property_flags & CPF_NativeAccessSpecifierPublic) != 0)
        {
            return AccessModifier::Public;
        }
        else if ((property_flags & CPF_NativeAccessSpecifierProtected) != 0)
        {
            return AccessModifier::Protected;
        }
        else if ((property_flags & CPF_NativeAccessSpecifierPrivate) != 0)
        {
            return AccessModifier::Private;
        }
        return AccessModifier::Public;
    }

    auto UEHeaderGenerator::create_string_literal(std::wstring_view string) -> std::wstring
    {
        std::wstring result;
        result.append(STR("TEXT(\""));

        bool previous_character_was_hex = false;

        for (wchar_t ch : string)
        {
            switch (ch)
            {
            case STR('\r'): {
                continue;
            }
            case STR('\n'): {
                result.append(STR("\\n"));
                previous_character_was_hex = false;
                break;
            }
            case STR('\\'): {
                result.append(STR("\\\\"));
                previous_character_was_hex = false;
                break;
            }
            case STR('\"'): {
                result.append(STR("\\\""));
                previous_character_was_hex = false;
                break;
            }
            default: {
                if (ch < 31 || ch >= 128)
                {
                    result.append(fmt::format(STR("\\x{:04X}"), ch));
                    previous_character_was_hex = true;
                }
                else
                {
                    // We close and open the literal (with TEXT) here in order to ensure that successive hex characters aren't
                    // appended to the hex sequence, causing a different number
                    if (previous_character_was_hex && iswxdigit(ch) != 0)
                    {
                        result.append(STR("\")TEXT(\""));
                    }
                    previous_character_was_hex = false;
                    result.push_back(ch);
                }
                break;
            }
            }
        }
        result.append(STR("\")"));
        return result;
    }

    auto UEHeaderGenerator::convert_module_name_to_api_name(std::wstring module_name) -> std::wstring
    {
        std::wstring uppercase_string = string_to_uppercase(std::move(module_name));
        uppercase_string.append(STR("_API"));
        return uppercase_string;
    }

    auto UEHeaderGenerator::add_module_and_sub_module_dependencies(std::set<StringType>& out_public, std::set<StringType>& out_private, UPackage* package) -> void
    {
        if (!package) return;
        const auto iterator = m_module_dependencies.find(package);
        if (iterator == m_module_dependencies.end()) return;
        for (auto dep : iterator->second.public_deps)
        {
            out_public.insert(get_module_name_for_package(dep));
        }
        for (auto dep : iterator->second.private_deps)
        {
            if (iterator->second.public_deps.contains(dep)) continue;
            out_private.insert(get_module_name_for_package(dep));
        }
    }

    auto UEHeaderGenerator::collect_blacklisted_parameter_names(UStruct* ustruct, bool skip_self) -> CaseInsensitiveSet
    {
        std::unordered_set<FName> result_set;
        if (skip_self) ustruct = ustruct->GetSuperStruct();
        for (; ustruct; ustruct = ustruct->GetSuperStruct())
        {
            for (auto prop : ustruct->ForEachProperty())
            {
                result_set.insert(prop->GetFName());
            }
        }
        /*
        Function parameters are allowed to conflict w/ method names
        if (auto uclass = Cast<UClass>(ustruct))
        {
            for (auto function : uclass->ForEachFunction())
            {
                result_set.insert(function->GetNamePrivate());
            }
        }
        */
        return result_set;
    }

    // TODO CannotImplementInterfaceInBlueprint is not exactly right,
    // TODO you can have interface with no implementable blueprint methods but that you can still implement in blueprint
    auto UEHeaderGenerator::generate_interface_flags(UClass* uinterface) const -> std::wstring
    {
        FlagFormatHelper flag_format_helper{};

        auto class_flags = uinterface->GetClassFlags();
        UClass* super_interface = uinterface->GetSuperClass();
        auto parent_class_flags = super_interface->GetClassFlags();

        auto class_own_flags = (EClassFlags)(class_flags & (~(parent_class_flags & CLASS_Inherit)));

        if ((class_own_flags & CLASS_MinimalAPI) != 0)
        {
            flag_format_helper.add_switch(STR("MinimalAPI"));
        }

        ClassBlueprintInfo blueprint_info = get_class_blueprint_info(uinterface);
        ClassBlueprintInfo parent_blueprint_info = get_class_blueprint_info(super_interface);

        if (blueprint_info.is_blueprintable)
        {
            if (!parent_blueprint_info.is_blueprintable)
            {
                flag_format_helper.add_switch(STR("Blueprintable"));
            }
        }
        else if (blueprint_info.is_blueprint_type)
        {
            if (!parent_blueprint_info.is_blueprint_type)
            {
                flag_format_helper.add_switch(STR("BlueprintType"));
            }
            flag_format_helper.get_meta()->add_switch(STR("CannotImplementInterfaceInBlueprint"));
        }

        return flag_format_helper.build_flag_string();
    }

    auto UEHeaderGenerator::generate_class_flags(UClass* uclass) const -> std::wstring
    {
        FlagFormatHelper flag_format_helper{};

        auto class_flags = uclass->GetClassFlags();
        UClass* super_class = uclass->GetSuperClass();
        auto parent_class_flags = super_class ? super_class->GetClassFlags() : EClassFlags::CLASS_None;

        auto class_own_flags = (EClassFlags)(class_flags & (~(parent_class_flags & CLASS_Inherit)));

        if ((class_own_flags & CLASS_MinimalAPI) != 0)
        {
            flag_format_helper.add_switch(STR("MinimalAPI"));
        }

        ClassBlueprintInfo blueprint_info = get_class_blueprint_info(uclass);
        ClassBlueprintInfo parent_blueprint_info{};
        if (super_class != NULL)
        {
            parent_blueprint_info = get_class_blueprint_info(super_class);
        }

        if (UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeAllPropertyBlueprintsReadWrite ||
            UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeAllTypesBlueprintable)
        {
            flag_format_helper.add_switch(STR("Blueprintable"));
        }
        else
        {
            if (blueprint_info.is_blueprintable)
            {
                if (!parent_blueprint_info.is_blueprintable)
                {
                    flag_format_helper.add_switch(STR("Blueprintable"));
                }
            }
            else if (blueprint_info.is_blueprint_type)
            {
                if (!parent_blueprint_info.is_blueprint_type)
                {
                    flag_format_helper.add_switch(STR("BlueprintType"));
                }
            }
        }

        if ((class_own_flags & CLASS_Deprecated) != 0)
        {
            flag_format_helper.add_switch(STR("Deprecated"));
        }
        if ((class_own_flags & CLASS_Abstract) != 0)
        {
            flag_format_helper.add_switch(STR("Abstract"));
        }

        if ((class_own_flags & CLASS_MinimalAPI) != 0)
        {
            flag_format_helper.add_switch(STR("MinimalAPI"));
        }
        if ((class_own_flags & CLASS_NoExport) != 0)
        {
            flag_format_helper.add_switch(STR("NoExport"));
        }
        // TODO not quite the case, because UHT boilerplate implicitly marks every native class as CLASS_Intrinsic
        // if ((ClassOwnFlags & CLASS_Intrinsic) != 0) {
        //     FlagFormatHelper.AddSwitch(STR("Intrinsic"));
        // }

        if ((class_own_flags & CLASS_Const) != 0)
        {
            flag_format_helper.add_switch(STR("Const"));
        }
        if ((class_own_flags & CLASS_DefaultToInstanced) != 0)
        {
            flag_format_helper.add_switch(STR("DefaultToInstanced"));
        }

        UClass* class_within = uclass->GetClassWithin();
        if (class_within != NULL && class_within != UObject::StaticClass() && (super_class == NULL || class_within != super_class->GetClassWithin()))
        {
            flag_format_helper.add_parameter(STR("Within"), class_within->GetName());
        }

        if ((class_own_flags & CLASS_Transient) != 0)
        {
            flag_format_helper.add_switch(STR("Transient"));
        }
        else if ((parent_class_flags & CLASS_Transient) != 0)
        {
            flag_format_helper.add_switch(STR("NonTransient"));
        }

        if ((class_own_flags & CLASS_EditInlineNew) != 0)
        {
            flag_format_helper.add_switch(STR("EditInlineNew"));
        }
        else if ((class_flags & CLASS_EditInlineNew) == 0 && (parent_class_flags & CLASS_EditInlineNew) != 0)
        {
            flag_format_helper.add_switch(STR("NotEditInlineNew"));
        }

        if ((class_own_flags & CLASS_NotPlaceable) != 0)
        {
            flag_format_helper.add_switch(STR("NotPlaceable"));
        }
        else if ((class_flags & CLASS_NotPlaceable) == 0 && (parent_class_flags & CLASS_NotPlaceable) != 0)
        {
            flag_format_helper.add_switch(STR("Placeable"));
        }

        if ((class_own_flags & CLASS_DefaultConfig) != 0)
        {
            flag_format_helper.add_switch(STR("DefaultConfig"));
        }
        if ((class_own_flags & CLASS_GlobalUserConfig) != 0)
        {
            flag_format_helper.add_switch(STR("GlobalUserConfig"));
        }
        if ((class_own_flags & CLASS_ProjectUserConfig) != 0)
        {
            flag_format_helper.add_switch(STR("ProjectUserConfig"));
        }
        bool has_config_property = false;
        for (FProperty* property : uclass->ForEachProperty())
        {
            if (property->HasAnyPropertyFlags(CPF_Config | CPF_GlobalConfig))
            {
                has_config_property = true;
                break;
            }
        }
        bool has_config = class_has_config(uclass);
        if (has_config_property && !has_config)
        {
            Output::send<LogLevel::Warning>(STR("Class {} has config property but is not config\n"), uclass->GetFullName());
        }
        if (has_config)
        {
            auto config_name = uclass->GetClassConfigName();
            // UObject has ClassConfigName="Engine", but not config, so we must specify
            if (!super_class || super_class->GetClassConfigName() != config_name || !class_has_config(super_class))
            {
                flag_format_helper.add_parameter(STR("Config"), config_name.ToString());
            }
        }

        if ((class_own_flags & CLASS_PerObjectConfig) != 0)
        {
            flag_format_helper.add_switch(STR("PerObjectConfig"));
        }
        if ((class_own_flags & CLASS_ConfigDoNotCheckDefaults) != 0)
        {
            flag_format_helper.add_switch(STR("ConfigDoNotCheckDefaults"));
        }

        if ((class_own_flags & CLASS_HideDropDown) != 0)
        {
            flag_format_helper.add_switch(STR("HideDropdown"));
        }

        if ((class_own_flags & CLASS_CollapseCategories) != 0)
        {
            flag_format_helper.add_switch(STR("CollapseCategories"));
        }
        else if ((parent_class_flags & CLASS_CollapseCategories) != 0)
        {
            flag_format_helper.add_switch(STR("DontCollapseCategories"));
        }

        // Mark all UActorComponent derived classes as BlueprintSpawnableComponent by default
        // This will allow using them inside the Simple Construction Script of the blueprint assets
        if (uclass->IsChildOf<UActorComponent>())
        {
            flag_format_helper.get_meta()->add_switch(STR("BlueprintSpawnableComponent"));
            flag_format_helper.add_parameter(STR("ClassGroup"), STR("Custom"));
        }

        return flag_format_helper.build_flag_string();
    }

    /**/
    auto UEHeaderGenerator::generate_property_type_declaration(FProperty* property, const PropertyTypeDeclarationContext& context) -> std::wstring
    {
        UClass* current_class = Unreal::Cast<UClass>(property->GetOutermostOwner());
        const std::wstring field_class_name = property->GetClass().GetName();

        if (auto prop = CastField<FByteProperty>(property))
        {
            auto uenum = prop->GetEnum();
            if (uenum)
            {
                if (context.source_file)
                {
                    context.source_file->add_dependency(uenum, DependencyLevel::Include);
                }
                auto enum_name = get_native_enum_name(uenum);
                // see FByteProperty::GetCPPType
                if (uenum->GetCppForm() == UEnum::ECppForm::EnumClass || context.is_parm && (prop->HasAnyPropertyFlags(CPF_ReturnParm) || !prop->HasAnyPropertyFlags(CPF_OutParm)))
                {
                    return enum_name;
                }
                else
                {
                    return fmt::format(STR("TEnumAsByte<{}>"), enum_name);
                }
            }
            return STR("uint8");
        }
        if (auto prop = CastField<FEnumProperty>(property))
        {
            auto underlying_prop = prop->GetUnderlyingProperty();
            UEnum* uenum = prop->GetEnum();
            if (context.source_file)
            {
                context.source_file->add_dependency(uenum, DependencyLevel::Include);
            }
            return get_native_enum_name(uenum);
        }
        if (auto prop = CastField<FBoolProperty>(property))
        {
            if (context.is_top_level_declaration && context.out_is_bitmask_bool && prop->GetFieldMask() != 255)
            {
                *context.out_is_bitmask_bool = true;
                return STR("uint8");
            }
            return STR("bool");
        }
        // Standard Numeric Properties
        if (property->IsA<FInt8Property>())
        {
            return STR("int8");
        }
        if (property->IsA<FInt16Property>())
        {
            return STR("int16");
        }
        if (property->IsA<FIntProperty>())
        {
            return STR("int32");
        }
        if (property->IsA<FInt64Property>())
        {
            return STR("int64");
        }
        if (property->IsA<FUInt16Property>())
        {
            return STR("uint16");
        }
        if (property->IsA<FUInt32Property>())
        {
            return STR("uint32");
        }
        if (property->IsA<FUInt64Property>())
        {
            return STR("uint64");
        }
        if (property->IsA<FFloatProperty>())
        {
            return STR("float");
        }
        if (property->IsA<FDoubleProperty>())
        {
            return STR("double");
        }

        // Class Properties
        if (property->IsA<FClassProperty>() || property->IsA<FAssetClassProperty>())
        {
            auto prop = static_cast<FClassProperty*>(property);
            auto metaclass = prop->GetMetaClass();
            if (!metaclass || metaclass == UObject::StaticClass())
            {
                return STR("UClass*");
            }

            if (context.source_file)
            {
                // the template calls a member function, so we need full inclusion
                context.source_file->add_dependency(metaclass, DependencyLevel::Include);
                context.source_file->add_extra_include(STR("Templates/SubclassOf.h"));
            }
            auto metaclass_name = get_native_class_name(metaclass);
            return fmt::format(STR("TSubclassOf<{}>"), metaclass_name);
        }
        if (auto prop = CastField<FClassPtrProperty>(property))
        {
            // TODO: Confirm that this is accurate
            return STR("TClassPtr<UClass>");
        }
        if (auto prop = CastField<FSoftClassProperty>(property))
        {
            auto metaclass = prop->GetMetaClass();
            if (!metaclass || metaclass == UObject::StaticClass())
            {
                return STR("TSoftClassPtr<UObject>");
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(metaclass, DependencyLevel::PreDeclaration);
            }
            auto metaclass_name = get_native_class_name(metaclass);
            return fmt::format(STR("TSoftClassPtr<{}>"), metaclass_name);
        }
        // Object Properties
        //  TODO: Verify that the syntax for 'AssetObjectProperty' is the same as for 'ObjectProperty'.
        //        If it's not, then add another branch here after you figure out what the syntax should be.
        if (property->IsA<FObjectProperty>() || property->IsA<FAssetObjectProperty>())
        {
            auto prop = static_cast<FObjectProperty*>(property);
            auto property_class = prop->GetPropertyClass();
            if (!property_class)
            {
                return STR("UObject*");
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(property_class, DependencyLevel::PreDeclaration);
            }
            auto property_class_name = get_native_class_name(property_class);
            return fmt::format(STR("{}*"), property_class_name);
        }

        if (auto prop = CastField<FObjectPtrProperty>(property))
        {
            auto property_class = prop->GetPropertyClass();
            if (!property_class)
            {
                return STR("TObjectPtr<UObject>");
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(property_class, DependencyLevel::PreDeclaration);
            }
            auto property_class_name = get_native_class_name(property_class);
            return fmt::format(STR("TObjectPtr<{}>"), property_class_name);
        }
        if (auto prop = CastField<FWeakObjectProperty>(property))
        {
            auto property_class = prop->GetPropertyClass();
            if (!property_class)
            {
                return STR("TWeakObjectPtr<UObject>");
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(property_class, DependencyLevel::PreDeclaration);
            }
            auto property_class_name = get_native_class_name(property_class);
            return fmt::format(STR("TWeakObjectPtr<{}>"), property_class_name);
        }
        if (auto prop = CastField<FLazyObjectProperty>(property))
        {
            auto property_class = prop->GetPropertyClass();
            if (!property_class)
            {
                return STR("TLazyObjectPtr<UObject>");
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(property_class, DependencyLevel::PreDeclaration);
            }
            auto property_class_name = get_native_class_name(property_class);
            return fmt::format(STR("TLazyObjectPtr<{}>"), property_class_name);
        }
        if (auto prop = CastField<FSoftObjectProperty>(property))
        {
            auto property_class = prop->GetPropertyClass();
            if (!property_class)
            {
                return STR("TSoftObjectPtr<UObject>");
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(property_class, DependencyLevel::PreDeclaration);
            }
            auto property_class_name = get_native_class_name(property_class);
            return fmt::format(STR("TSoftObjectPtr<{}>"), property_class_name);
        }
        if (auto prop = CastField<FInterfaceProperty>(property))
        {
            auto interface_class = prop->GetInterfaceClass();
            if (!interface_class || interface_class == UInterface::StaticClass())
            {
                return STR("FScriptInterface");
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(interface_class, DependencyLevel::PreDeclaration);
            }
            const std::wstring interface_class_name = get_native_class_name(interface_class, true);

            return fmt::format(STR("TScriptInterface<{}>"), interface_class_name);
        }
        if (auto prop = CastField<FStructProperty>(property))
        {
            auto script_struct = prop->GetStruct();
            if (context.source_file)
            {
                context.source_file->add_dependency(script_struct, DependencyLevel::Include);
            }
            return get_native_struct_name(script_struct);
        }
        // Delegate Properties
        if (auto prop = CastField<FDelegateProperty>(property))
        {
            UFunction* signature_func = prop->GetSignatureFunction();
            if (!signature_func)
            {
                throw std::runtime_error{fmt::format("FunctionSignature is nullptr, cannot deduce function for '{}'\n", to_string(prop->GetFullName()))};
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(signature_func, DependencyLevel::Include);
            }
            return get_native_delegate_type_name(signature_func, current_class);
        }
        // In 4.23, they replaced 'MulticastDelegateProperty' with 'Inline' & 'Sparse' variants
        // It looks like the delegate macro might be the same as the 'Inline' variant in later versions, so we'll use the same branch here
        if (auto prop = CastField<FMulticastDelegateProperty>(property))
        {
            UFunction* signature_func = prop->GetSignatureFunction();
            if (!signature_func)
            {
                throw std::runtime_error{fmt::format("FunctionSignature is nullptr, cannot deduce function for '{}'\n", to_string(prop->GetFullName()))};
            }
            if (context.source_file)
            {
                context.source_file->add_dependency(signature_func, DependencyLevel::Include);
            }
            return get_native_delegate_type_name(signature_func, current_class);
        }
        if (auto prop = CastField<FFieldPathProperty>(property))
        {
            auto native_class_name = get_native_field_class_name(prop->GetPropertyClass());
            return fmt::format(STR("TFieldPath<{}>"), native_class_name);
        }

        // Collection and Map Properties
        if (auto prop = CastField<FArrayProperty>(property))
        {
            // TODO: This is missing support for freeze image array properties because XArrayProperty is incomplete. (low priority)
            auto inner_prop = prop->GetInner();
            auto inner_str = generate_property_type_declaration(inner_prop, context.inner_context());
            return fmt::format(STR("TArray<{}>"), inner_str);
        }
        if (auto prop = CastField<FSetProperty>(property))
        {
            auto element_prop = prop->GetElementProp();
            auto element_type = generate_property_type_declaration(element_prop, context.inner_context());
            return fmt::format(STR("TSet<{}>"), element_type);
        }
        if (auto prop = CastField<FMapProperty>(property))
        {
            // TODO: This is missing support for freeze image map properties because XMapProperty is incomplete. (low priority)
            auto key_prop = prop->GetKeyProp();
            auto value_prop = prop->GetValueProp();
            auto key_type = generate_property_type_declaration(key_prop, context.inner_context());
            auto value_type = generate_property_type_declaration(value_prop, context.inner_context());
            return fmt::format(STR("TMap<{}, {}>"), key_type, value_type);
        }
        // Standard properties that do not have any special attributes
        if (property->IsA<FNameProperty>())
        {
            return STR("FName");
        }
        if (property->IsA<FStrProperty>())
        {
            return STR("FString");
        }
        if (property->IsA<FTextProperty>())
        {
            return STR("FText");
        }
        throw std::runtime_error(RC::fmt("[generate_property_type_declaration] Unsupported property class '%S', full name: '%S'",
                                         field_class_name.c_str(),
                                         property->GetFullName().c_str()));
    }
    //*/

    static auto needs_reference_workaround(FProperty* property) -> bool
    {
        auto cp = CastField<FClassProperty>(property);
        return property->HasAllPropertyFlags(CPF_ReferenceParm | CPF_OutParm | CPF_ConstParm) && property->IsA<FObjectProperty>() && (!cp || !cp->GetMetaClass() || cp->GetMetaClass() == UObject::StaticClass());
    }
    auto UEHeaderGenerator::generate_function_argument_flags(FProperty* property) const -> std::wstring
    {
        FlagFormatHelper flag_format_helper{};
        auto property_flags = property->GetPropertyFlags();

        // CPF_ConstParm is handled explicitly in the parameter list generator, it will generate const before parameter
        // if ((PropertyFlags & CPF_ConstParm) != 0) {
        //     FlagFormatHelper.AddSwitch(STR("Const"));
        // }

        // We only want to add UPARAM(Ref) when parameter is marked as reference AND output,
        // while not being marked as constant, because if it's marked as constant, it might be a parameter passed by const reference
        if (property->HasAllPropertyFlags(CPF_ReferenceParm | CPF_OutParm) && !property->HasAnyPropertyFlags(CPF_ConstParm) || needs_reference_workaround(property))
        {
            flag_format_helper.add_switch(STR("Ref"));
        }

        if ((property_flags & CPF_RepSkip) != 0)
        {
            flag_format_helper.add_switch(STR("NotReplicated"));
        }
        return flag_format_helper.build_flag_string();
    }

    auto UEHeaderGenerator::generate_property_flags(UStruct* ustruct, FProperty* property) const -> std::wstring
    {
        FlagFormatHelper flag_format_helper{};
        auto property_flags = property->GetPropertyFlags();

        bool deprecated = property_flags & CPF_Deprecated;

        if (!deprecated && UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeAllPropertyBlueprintsReadWrite)
        {
            flag_format_helper.add_switch(STR("EditAnywhere"));
        }
        else if ((property_flags & CPF_Edit) != 0)
        {
            if ((property_flags & CPF_EditConst) != 0)
            {
                if ((property_flags & CPF_DisableEditOnTemplate) != 0)
                {
                    flag_format_helper.add_switch(STR("VisibleInstanceOnly"));
                }
                else if ((property_flags & CPF_DisableEditOnInstance) != 0)
                {
                    flag_format_helper.add_switch(STR("VisibleDefaultsOnly"));
                }
                else
                {
                    flag_format_helper.add_switch(STR("VisibleAnywhere"));
                }
            }
            else
            {
                if ((property_flags & CPF_DisableEditOnTemplate) != 0)
                {
                    flag_format_helper.add_switch(STR("EditInstanceOnly"));
                }
                else if ((property_flags & CPF_DisableEditOnInstance) != 0)
                {
                    flag_format_helper.add_switch(STR("EditDefaultsOnly"));
                }
                else
                {
                    flag_format_helper.add_switch(STR("EditAnywhere"));
                }
            }
        }

        if ((property_flags & CPF_NoClear) != 0)
        {
            flag_format_helper.add_switch(STR("NoClear"));
        }
        if ((property_flags & CPF_EditFixedSize) != 0)
        {
            flag_format_helper.add_switch(STR("EditFixedSize"));
        }
        if ((property_flags & CPF_SimpleDisplay) != 0)
        {
            flag_format_helper.add_switch(STR("SimpleDisplay"));
        }
        if ((property_flags & CPF_AdvancedDisplay) != 0)
        {
            flag_format_helper.add_switch(STR("AdvancedDisplay"));
        }

        if (deprecated)
        {
            // nothing
        }
        else if (UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeAllPropertyBlueprintsReadWrite)
        {
            if (property->GetArrayDim() == 1 && is_subtype_valid(property))
            {
                flag_format_helper.add_switch(STR("BlueprintReadWrite"));
            }
            flag_format_helper.get_meta()->add_parameter(STR("AllowPrivateAccess"), STR("true"));
        }
        else if ((property_flags & CPF_BlueprintVisible) != 0)
        {
            if ((property_flags & CPF_BlueprintReadOnly) != 0)
            {
                flag_format_helper.add_switch(STR("BlueprintReadOnly"));
            }
            else
            {
                flag_format_helper.add_switch(STR("BlueprintReadWrite"));
            }
            if ((property_flags & CPF_NativeAccessSpecifierPrivate) != 0)
            {
                flag_format_helper.get_meta()->add_parameter(STR("AllowPrivateAccess"), STR("true"));
            }
        }

        if ((property_flags & CPF_BlueprintAssignable) != 0)
        {
            flag_format_helper.add_switch(STR("BlueprintAssignable"));
        }
        if ((property_flags & CPF_BlueprintCallable) != 0)
        {
            flag_format_helper.add_switch(STR("BlueprintCallable"));
        }
        if ((property_flags & CPF_BlueprintAuthorityOnly) != 0)
        {
            flag_format_helper.add_switch(STR("BlueprintAuthorityOnly"));
        }

        if ((property_flags & CPF_Config) != 0)
        {
            if ((property_flags & CPF_GlobalConfig) != 0)
            {
                flag_format_helper.add_switch(STR("GlobalConfig"));
            }
            else
            {
                flag_format_helper.add_switch(STR("Config"));
            }
        }

        if ((property_flags & CPF_Net) != 0)
        {
            if ((property_flags & CPF_RepNotify) != 0)
            {
                const std::wstring rep_notify_func_name = property->GetRepNotifyFunc().ToString();
                flag_format_helper.add_parameter(STR("ReplicatedUsing"), rep_notify_func_name);
            }
            else
            {
                flag_format_helper.add_switch(STR("Replicated"));
            }
        }
        if ((property_flags & CPF_RepSkip) != 0)
        {
            flag_format_helper.add_switch(STR("NotReplicated"));
        }

        if ((property_flags & CPF_AssetRegistrySearchable) != 0)
        {
            flag_format_helper.add_switch(STR("AssetRegistrySearchable"));
        }
        if ((property_flags & CPF_Interp) != 0)
        {
            flag_format_helper.add_switch(STR("Interp"));
        }
        if ((property_flags & CPF_SaveGame) != 0)
        {
            flag_format_helper.add_switch(STR("SaveGame"));
        }
        if ((property_flags & CPF_NonTransactional) != 0)
        {
            flag_format_helper.add_switch(STR("NonTransactional"));
        }

        if ((property_flags & CPF_Transient) != 0)
        {
            flag_format_helper.add_switch(STR("Transient"));
        }
        if ((property_flags & CPF_DuplicateTransient) != 0)
        {
            flag_format_helper.add_switch(STR("DuplicateTransient"));
        }
        if ((property_flags & CPF_TextExportTransient) != 0)
        {
            flag_format_helper.add_switch(STR("TextExportTransient"));
        }
        if ((property_flags & CPF_NonPIEDuplicateTransient) != 0)
        {
            flag_format_helper.add_switch(STR("NonPIEDuplicateTransient"));
        }
        if ((property_flags & CPF_SkipSerialization) != 0)
        {
            flag_format_helper.add_switch(STR("SkipSerialization"));
        }

        if (auto prop = CastField<FObjectProperty>(property))
        {
            static UClass* UWidget_StaticClass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/UMG.Widget"));
            if (prop->GetPropertyClass()->IsChildOf(UWidget_StaticClass) && bind_widget.includes(prop))
            {
                flag_format_helper.get_meta()->add_switch(STR("BindWidget"));
            }
        }

        // Need to have all of these flags, otherwise we might accidentally get Instanced of delegate properties; CPF_ExportObject is not set for delegate properties
        uint64_t instanced_flags = CPF_ExportObject | CPF_InstancedReference;

        // Instanced Arrays use CPF_ContainsInstancedReference instead of CPF_InstancedReference
        uint64_t instanced_array_flags = CPF_ExportObject | CPF_ContainsInstancedReference;

        if (((property_flags & instanced_flags) == instanced_flags || (property_flags & instanced_array_flags) == instanced_array_flags) &&
            (property->IsA<FObjectProperty>() || (property->IsA<FArrayProperty>() && static_cast<FArrayProperty*>(property)->GetInner()->IsA<FObjectProperty>()) ||
             (property->IsA<FMapProperty>() && static_cast<FMapProperty*>(property)->GetValueProp()->IsA<FObjectProperty>())))
        {
            flag_format_helper.add_switch(STR("Instanced"));
        }
        else if ((property_flags & CPF_ExportObject) != 0)
        {
            flag_format_helper.add_switch(STR("Export"));
        }
        return flag_format_helper.build_flag_string();
    }

    auto UEHeaderGenerator::generate_struct_flags(UScriptStruct* script_struct) const -> std::wstring
    {
        FlagFormatHelper flag_format_helper{};

        UScriptStruct* super_struct = script_struct->GetSuperScriptStruct();
        EStructFlags parent_struct_flags = (EStructFlags)(super_struct ? (super_struct->GetStructFlags() & (~STRUCT_ComputedFlags)) : STRUCT_NoFlags);
        EStructFlags struct_flags = (EStructFlags)(script_struct->GetStructFlags() & (~STRUCT_ComputedFlags));

        EStructFlags struct_own_flags = (EStructFlags)(struct_flags & (~(parent_struct_flags & STRUCT_Inherit)));

        const std::wstring native_struct_name = get_native_struct_name(script_struct);
        if (is_struct_blueprint_visible(script_struct) ||
            UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeAllPropertyBlueprintsReadWrite ||
            UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeAllTypesBlueprintable)
        {
            flag_format_helper.add_switch(STR("BlueprintType"));
        }

        if ((struct_own_flags & STRUCT_NoExport) != 0)
        {
            flag_format_helper.add_switch(STR("NoExport"));
        }
        if ((struct_own_flags & STRUCT_Atomic) != 0)
        {
            flag_format_helper.add_switch(STR("Atomic"));
        }
        if ((struct_own_flags & STRUCT_Immutable) != 0)
        {
            flag_format_helper.add_switch(STR("Immutable"));
        }
        return flag_format_helper.build_flag_string();
    }

    auto UEHeaderGenerator::generate_enum_flags(UEnum* uenum) const -> std::wstring
    {

        FlagFormatHelper flag_format_helper{};

        auto enum_flags = uenum->GetEnumFlags();

        if ((((int32_t)enum_flags) & ((int32_t)EEnumFlags::Flags)) != 0)
        {
            flag_format_helper.add_switch(STR("Flags"));
        }
        if (UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeEnumClassesBlueprintType)
        {
            auto cpp_form = uenum->GetCppForm();
            if (cpp_form == UEnum::ECppForm::EnumClass)
            {
                const auto underlying_prop = m_underlying_enum_props.find(uenum);
                if (underlying_prop == m_underlying_enum_props.end() ? get_lowest_enum(uenum) >= 0 && get_highest_enum(uenum) <= 255 : underlying_prop->second->IsA<FByteProperty>())
                {
                    // Underlying type is implicit or explicitly uint8.
                    flag_format_helper.add_switch(STR("BlueprintType"));
                }
            }
            else
            {
                flag_format_helper.add_switch(STR("BlueprintType"));
            }
        }
        return flag_format_helper.build_flag_string();
    }

    auto UEHeaderGenerator::sanitize_enumeration_name(std::wstring enum_name) -> std::wstring
    {
        // Remove enumeration name from the string
        size_t enum_name_string_split = enum_name.find(STR("::"));
        if (enum_name_string_split != std::wstring::npos)
        {
            enum_name.erase(0, enum_name_string_split + 2);
        }
        return enum_name;
    }

    auto UEHeaderGenerator::get_highest_enum(UEnum* uenum) -> int64_t
    {
        if (!uenum || uenum->NumEnums() <= 0)
        {
            return 0;
        }

        int64 highest_enum_value = 0;
        const StringType enum_prefix = uenum->GenerateEnumPrefix();
        const StringType expected_max_name = fmt::format(STR("{}_MAX"), enum_prefix);
        StringType expected_max_name_lower = expected_max_name;
        std::transform(expected_max_name_lower.begin(), expected_max_name_lower.end(), expected_max_name_lower.begin(), ::towlower);

        for (auto [Name, Value] : uenum->ForEachName())
        {
            StringType enum_name = sanitize_enumeration_name(Name.ToString());
            StringType enum_name_lower = enum_name;
            std::transform(enum_name_lower.begin(), enum_name_lower.end(), enum_name_lower.begin(), ::towlower);
            if ((enum_name_lower != expected_max_name_lower && enum_name_lower != sanitize_enumeration_name(expected_max_name_lower)) && Value > highest_enum_value)
            {
                highest_enum_value = Value;
            }
        }
        return highest_enum_value;
    }

    auto UEHeaderGenerator::get_lowest_enum(UEnum* uenum) -> int64_t
    {
        if (!uenum || uenum->NumEnums() <= 0)
        {
            return 0;
        }

        int64 lowest_enum_value = 0;
        for (auto [Name, Value] : uenum->ForEachName())
        {
            if (Value < lowest_enum_value)
            {
                lowest_enum_value = Value;
            }
        }
        return lowest_enum_value;
    }

    auto UEHeaderGenerator::generate_function_flags(UFunction* function, bool is_function_pure_virtual) const -> std::wstring
    {
        FlagFormatHelper flag_format_helper{};

        auto function_flags = function->GetFunctionFlags();

        UObject* outer_object = function->GetOuterPrivate();
        bool is_interface_function = false;

        if (outer_object->GetClassPrivate()->IsChildOf(UClass::StaticClass()))
        {
            UClass* outer_class = (UClass*)outer_object;
            is_interface_function = (outer_class->GetClassFlags() & CLASS_Interface) != 0;
        }

        bool blueprint_callable_added = false;
        if ((function_flags & FUNC_BlueprintCallable) != 0)
        {
            // Interface functions cannot be BlueprintPure
            if ((function_flags & FUNC_BlueprintPure) != 0 && !is_interface_function)
            {
                flag_format_helper.add_switch(STR("BlueprintPure"));
            }
            else
            {
                // If function is marked as FUNC_Const but not as BlueprintPure,
                // it has been explicitly marked as blueprint impure, and we need to preserve this behavior
                if ((function_flags & FUNC_Const) != 0 && !is_interface_function)
                {
                    flag_format_helper.add_parameter(STR("BlueprintPure"), STR("false"));
                }
                flag_format_helper.add_switch(STR("BlueprintCallable"));
                blueprint_callable_added = true;
            }
        }
        if (!blueprint_callable_added && UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeAllFunctionsBlueprintCallable && !is_function_pure_virtual)
        {
            bool has_invalid_param{};
            for (FProperty* param : function->ForEachProperty())
            {
                if (!is_subtype_valid(param))
                {
                    has_invalid_param = true;
                    break;
                }
            }

            if (!has_invalid_param)
            {
                flag_format_helper.add_switch(STR("BlueprintCallable"));
            }
        }

        if ((function_flags & FUNC_BlueprintEvent) != 0)
        {
            if ((function_flags & FUNC_Native) != 0)
            {
                flag_format_helper.add_switch(STR("BlueprintNativeEvent"));
            }
            else
            {
                flag_format_helper.add_switch(STR("BlueprintImplementableEvent"));
            }
        }

        if ((function_flags & FUNC_Net) != 0)
        {
            if ((function_flags & FUNC_NetServer) != 0)
            {
                flag_format_helper.add_switch(STR("Server"));
            }
            else if ((function_flags & FUNC_NetClient) != 0)
            {
                flag_format_helper.add_switch(STR("Client"));
            }
            else if ((function_flags & FUNC_NetMulticast) != 0)
            {
                flag_format_helper.add_switch(STR("NetMulticast"));
            }
            else if ((function_flags & FUNC_NetRequest) != 0)
            {
                flag_format_helper.add_switch(STR("ServiceRequest"));
            }
            else if ((function_flags & FUNC_NetResponse) != 0)
            {
                flag_format_helper.add_switch(STR("ServiceResponse"));
            }

            if ((function_flags & FUNC_NetReliable) != 0)
            {
                flag_format_helper.add_switch(STR("Reliable"));
            }
            else
            {
                flag_format_helper.add_switch(STR("Unreliable"));
            }
            if ((function_flags & FUNC_NetValidate) != 0)
            {
                flag_format_helper.add_switch(STR("WithValidation"));
            }
        }

        if ((function_flags & FUNC_Exec) != 0)
        {
            flag_format_helper.add_switch(STR("Exec"));
        }
        if ((function_flags & FUNC_BlueprintAuthorityOnly) != 0)
        {
            flag_format_helper.add_switch(STR("BlueprintAuthorityOnly"));
        }
        if ((function_flags & FUNC_BlueprintCosmetic) != 0)
        {
            flag_format_helper.add_switch(STR("BlueprintCosmetic"));
        }

        static auto latent_action_info = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/Engine.LatentActionInfo"));
        bool found_wco = false;
        bool found_lai = false;
        //static auto EBoolExecPin = UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr, STR("/Script/BF_FrameworkBase.EBoolExecPin"));
        //static auto EValidNotValidExecPin = UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr, STR("/Script/BF_FrameworkBase.EValidNotValidExecPin"));
        //static auto EBuildConfigurationExecPin =
        //        UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr, STR("/Script/BF_FrameworkBase.EBuildConfigurationExecPin"));
        //bool found_exec = false;
        for (FProperty* param : function->ForEachProperty())
        {
            static auto NAME_InWCO = FName(STR("InWCO"), FNAME_Add);
            static auto NAME_WCO = FName(STR("WCO"), FNAME_Add);
            auto param_name = param->GetFName();
            auto param_name_str = param_name.ToString();
            if (param_name == NAME_InWCO || param_name == NAME_WCO || string_contains_ci(param_name_str, STR("WorldContext")))
            {
                flag_format_helper.get_meta()->add_parameter(STR("WorldContext"), param_name_str);
                found_wco = true;
            }
            if (auto prop = CastField<FStructProperty>(param))
            {
                if (prop->GetStruct()->IsChildOf(latent_action_info))
                {
                    flag_format_helper.get_meta()->add_parameter(STR("LatentInfo"), param_name_str);
                    flag_format_helper.get_meta()->add_switch(STR("Latent"));
                    found_lai = true;
                }
            }
            //auto enum_prop = CastField<FEnumProperty>(param);
            //auto num_prop = CastField<FNumericProperty>(param);
            //auto underlying = enum_prop ? enum_prop->GetEnum() : num_prop ? num_prop->GetIntPropertyEnum() : nullptr;
            //if (underlying && (underlying == EBoolExecPin || underlying == EValidNotValidExecPin || underlying == EBuildConfigurationExecPin))
            //{
            //    flag_format_helper.get_meta()->add_parameter(STR(""));
            //    found_exec = true;
            //}
            if (found_wco && found_lai/* && found_exec*/) break;
        }

        return flag_format_helper.build_flag_string();
    }

    auto UEHeaderGenerator::generate_function_parameter_list(UClass* uclass,
                                                             UFunction* function,
                                                             GeneratedSourceFile& header_data,
                                                             bool generate_comma_before_name,
                                                             std::wstring_view context_name,
                                                             const CaseInsensitiveSet& blacklisted_parameter_names,
                                                             int32_t* out_num_params) -> std::wstring
    {
        std::wstring function_arguments_string;

        for (FProperty* property : function->ForEachProperty())
        {
            auto property_flags = property->GetPropertyFlags();
            if ((property_flags & CPF_Parm) != 0 && (property_flags & CPF_ReturnParm) == 0)
            {
                std::wstring param_declaration;

                // We only generate UPARAM declarations if we are generating the header
                if (header_data.is_header())
                {
                    const std::wstring parameter_flags_string = generate_function_argument_flags(property);
                    if (!parameter_flags_string.empty())
                    {
                        param_declaration.append(STR("UPARAM("));
                        param_declaration.append(parameter_flags_string);
                        param_declaration.append(STR(") "));
                    }
                }

                // Force const reference when we're dealing with strings, and they are not passed by reference
                // UHT for whatever reason completely strips const and reference flags from string properties, but happily generates them in boilerplate code
                const bool should_force_const_ref = (property_flags & (CPF_ReferenceParm | CPF_OutParm)) == 0 && property->IsA<FStrProperty>();

                // Append const keyword to the parameter declaration if it is marked as const parameter
                if ((property_flags & CPF_ConstParm) != 0 || should_force_const_ref)
                {
                    param_declaration.append(STR("const "));
                }

                PropertyTypeDeclarationContext context(context_name, &header_data, false, true);
                param_declaration.append(generate_property_type_declaration(property, context));

                if (needs_reference_workaround(property))
                {
                    // UHT chokes on `const UObject*&`, so we generate `UPARAM(ref) const UOBJECT*` instead in generate_function_argument_flags
                }
                // Reference will be appended if the parameter has a CPF_ReferenceParm flag set,
                // which would also be always set for output parameters
                else if ((property_flags & (CPF_ReferenceParm | CPF_OutParm)) != 0 || should_force_const_ref)
                {
                    param_declaration.append(STR("&"));
                }

                if (generate_comma_before_name)
                {
                    param_declaration.append(STR(","));
                }
                param_declaration.append(STR(" "));

                auto property_name = property->GetFName();
                auto property_name_str = property_name.ToString();
                fix_cpp_keyword_name(property_name_str);

                // If property name is blacklisted, capitalize first letter and prepend New
                if (blacklisted_parameter_names.contains(property_name))
                {
                    property_name_str[0] = towupper(property_name_str[0]);
                    property_name_str.insert(0, STR("New"));
                    Output::send<LogLevel::Warning>(STR("Renaming shadowed property {}\n"), property->GetFullName());
                }
                param_declaration.append(property_name_str);

                function_arguments_string.append(param_declaration);
                function_arguments_string.append(STR(", "));
                if (out_num_params)
                {
                    (*out_num_params)++;
                }
            }
        }

        // remove trailing comma and space from the arguments string
        if (!function_arguments_string.empty())
        {
            function_arguments_string.erase(function_arguments_string.size() - 2);
        }
        return function_arguments_string;
    }

    auto UEHeaderGenerator::generate_property_value(UStruct* this_struct, FProperty* property, void const* data, GeneratedSourceFile& implementation_file) -> StringType
    {
        if (property->GetArrayDim() == 1)
        {
            return generate_property_element_value(this_struct, property, data, implementation_file);
        }
        StringType out = STR("{");
        char const* ptr = static_cast<char const*>(data);
        char const* end = ptr + property->GetSize();
        for (; ptr != end; ptr += property->GetElementSize())
        {
            if (ptr != data) out += STR(", ");
            out += generate_property_element_value(this_struct, property, ptr, implementation_file);
        }
        out += '}';
        return out;
    }

    auto DefaultSubobjects::insert_or_update(UObject* value) -> void
    {
        insert_or_update(value->GetNamePrivate(), value);
    }
    auto DefaultSubobjects::insert_or_update(FName name, UObject* value) -> void
    {
        auto [it, inserted] = objects.insert({name, objects_order.size()});
        if (inserted)
        {
            objects_order.push_back({name, value});
        }
        else
        {
            objects_order[it->second].second = value;
        }
    }
    auto DefaultSubobjects::find(FName name) const -> std::pair<UObject*, bool>
    {
        auto it = objects.find(name);
        if (it == objects.end()) return {nullptr, false};
        return {objects_order[it->second].second, true};
    }

    auto UEHeaderGenerator::generate_class_value(UClass* value, UClass* metaclass, GeneratedSourceFile& implementation_file) -> StringType
    {
        if (value->HasAnyClassFlags(CLASS_Native))
        {
            implementation_file.add_dependency(value, DependencyLevel::Include);
            return fmt::format(STR("{}::StaticClass()"), get_native_class_name(value));
        }
        return generate_object_finder(metaclass, value->GetPathName(), implementation_file, true);
    }
    auto UEHeaderGenerator::generate_struct_value(UScriptStruct* value, GeneratedSourceFile& implementation_file) -> StringType
    {
        if (value->HasAnyStructFlags(STRUCT_Native))
        {
            implementation_file.add_dependency(value, DependencyLevel::Include);
            return fmt::format(STR("TBaseStructure<{}>::Get()"), get_native_struct_name(value));
        }
        Output::send<LogLevel::Error>(STR("Unimplemented struct (non-native) {}\n"), value->GetFullName());
        return fmt::format(STR("/*{}*/"), value->GetName());
    }
    auto UEHeaderGenerator::generate_object_property_element_value(UStruct* this_struct, FProperty* prop, UObject* value, UClass* metaclass, GeneratedSourceFile& implementation_file) -> StringType
    {
        if (!value)
        {
            return STR("nullptr");
        }
        auto value_class = value->GetClassPrivate();
        if (auto val = Cast<UClass>(value))
        {
            return generate_class_value(val, metaclass, implementation_file);
        }
        if (value->HasAnyFlags(RF_ClassDefaultObject))
        {
            auto class_name = get_native_class_name(metaclass);
            auto value_str = generate_class_value(value_class, UClass::StaticClass(), implementation_file);
            return fmt::format(STR("GetMutableDefault<{}>({})"), class_name, value_str);
        }
        if (value->HasAnyFlags(RF_DefaultSubObject))
        {
            return generate_dso_value(this_struct, value, implementation_file);
        }
        if (value->HasAnyFlags(RF_Public))
        {
            return generate_object_finder(value_class, value->GetPathName(), implementation_file, false);
        }
        Output::send<LogLevel::Warning>(STR("Unhandle value of {} = {}\n"), prop->GetFullName(), value->GetFullName());
        return STR("nullptr");
    }
    auto UEHeaderGenerator::generate_property_element_value(UStruct* this_struct, FProperty* property, void const* data, GeneratedSourceFile& implementation_file) -> StringType
    {
        if (auto prop = CastField<FUInt64Property>(property))
        {
            uint64_t value = prop->GetUnsignedIntPropertyValue(const_cast<void*>(data));
            return std::to_wstring(value);
        }
        if (auto prop = CastField<FFloatProperty>(property))
        {
            auto value = prop->GetPropertyValue(data);
            return fmt::format(STR("{:.9e}f"), value);
        }
        if (auto prop = CastField<FDoubleProperty>(property))
        {
            auto value = prop->GetPropertyValue(data);
            return fmt::format(STR("{:.17e}"), value);
        }
        if (auto prop = CastField<FEnumProperty>(property))
        {
            auto uenum = prop->GetEnum();
            auto underlying = prop->GetUnderlyingProp();
            auto value = underlying->GetSignedIntPropertyValue(const_cast<void*>(data));
            return generate_enum_value(uenum, value, implementation_file);
        }
        if (auto prop = CastField<FNumericProperty>(property))
        {
            int64_t value = prop->GetSignedIntPropertyValue(const_cast<void*>(data));
            if (auto uenum = prop->GetIntPropertyEnum())
            {
                return generate_enum_value(uenum, value, implementation_file);
            }
            return std::to_wstring(value);
        }
        if (auto prop = CastField<FBoolProperty>(property))
        {
            return prop->GetPropertyValue(data) ? STR("true") : STR("false");
        }
        if (auto prop = CastField<FNameProperty>(property))
        {
            auto const& value = prop->GetPropertyValue(data);
            return value.Equals(NAME_None) ? STR("NAME_None") : fmt::format(STR("TEXT(\"{}\")"), value.ToString());
        }
        if (auto prop = CastField<FStrProperty>(property))
        {
            auto const& value = prop->GetPropertyValue(data);
            return create_string_literal(value.GetCharArray());
        }
        if (auto prop = CastField<FTextProperty>(property))
        {
            auto const& value = prop->GetPropertyValue(data);
            if (!value.Data || value.ToFString().Len() == 0)
            {
                return STR("FText::GetEmpty()");
            }
            return fmt::format(STR("FText::FromString({})"), create_string_literal(value.ToString()));
        }
        if (auto prop = CastField<FClassProperty>(property))
        {
            auto value = Cast<UClass>(prop->GetPropertyValue(data));
            if (!value)
            {
                return STR("nullptr");
            }
            return generate_class_value(value, prop->GetMetaClass(), implementation_file);
        }
        if (auto prop = CastField<FInterfaceProperty>(property))
        {
            auto& value = prop->GetPropertyValue(data);
            if (!value.ObjectPointer)
            {
                return STR("nullptr");
            }
            Output::send<LogLevel::Warning>(STR("Unhandled value of {} = {}\n"), prop->GetFullName(), value.ObjectPointer->GetFullName());
            return STR("nullptr");
        }
        if (auto prop = CastField<FObjectProperty>(property))
        {
            auto value = prop->GetPropertyValue(data);
            auto metaclass = prop->GetPropertyClass();
            return generate_object_property_element_value(this_struct, prop, value, metaclass, implementation_file);
        }
        if (auto prop = CastField<FSoftObjectProperty>(property))
        {
            auto& value = prop->GetPropertyValue(data);
            return generate_soft_path(STR("FSoftObjectPath"), value.ObjectID);
        }
        if (auto prop = CastField<FLazyObjectProperty>(property))
        {
            auto value = prop->GetPropertyValue(data).WeakPtr.Get();
            auto metaclass = prop->GetPropertyClass();
            // TODO: not entirely sure this is correct
            return generate_object_property_element_value(this_struct, prop, value, metaclass, implementation_file);
        }
        if (auto prop = CastField<FWeakObjectProperty>(property))
        {
            auto value = prop->GetPropertyValue(data).Get();
            auto metaclass = prop->GetPropertyClass();
            return generate_object_property_element_value(this_struct, prop, value, metaclass, implementation_file);
        }
        if (auto prop = CastField<FStructProperty>(property))
        {
            auto script_struct = prop->GetStruct();
            auto native_name = get_native_struct_name(script_struct);
            implementation_file.add_dependency(script_struct, DependencyLevel::Include);

            auto it = struct_generators().find(script_struct->GetNamePrivate());
            if (it != struct_generators().end())
            {
                return (this->*it->second)(this_struct, native_name, prop, data, implementation_file);
            }

            auto arch_data = get_default_object(script_struct);
            if (is_default_value(property, data, arch_data))
            {
                return fmt::format(STR("{}{{}}"), native_name);
            }

            auto id = implementation_file.gen_id();
            auto root = fmt::format(STR("gen{}"), id);
            implementation_file.format_line(STR("{} {};"), native_name, root);
            PropertyScope property_scope{root};
            for (auto st = script_struct; st; st = st->GetSuperScriptStruct())
            {
                for (FProperty* child_prop : st->ForEachProperty())
                {
                    generate_property_assignment_in_container(this_struct, child_prop, data, arch_data, implementation_file, property_scope, true);
                }
            }
            return fmt::format(STR("MoveTemp({})"), root);
        }
        if (auto prop = CastField<FArrayProperty>(property))
        {
            auto inner_prop = prop->GetInner();
            auto& value = *static_cast<FScriptArray const*>(data);

            StringType out = STR("{");
            bool first = true;
            for (auto element : each_item(value, inner_prop))
            {
                if (first) first = false;
                else out += STR(", ");
                out += generate_property_value(this_struct, inner_prop, element, implementation_file);
            }
            out += '}';
            return out;
        }
        if (auto prop = CastField<FSetProperty>(property))
        {
            auto element_prop = prop->GetElementProp();
            auto& value = *static_cast<FScriptSet const*>(data);

            StringType out = STR("{");
            bool first = true;
            for (auto element : each_item(value, element_prop))
            {
                if (first) first = false;
                else out += STR(", ");
                out += generate_property_value(this_struct, element_prop, element, implementation_file);
            }
            out += '}';
            return out;
        }
        if (auto prop = CastField<FMapProperty>(property))
        {
            auto key_prop = prop->GetKeyProp();
            auto value_prop = prop->GetValueProp();
            auto& value = *static_cast<FScriptSet const*>(data);

            StringType out = STR("{");
            bool first = true;
            for (auto [key, value] : each_item(value, key_prop, value_prop))
            {
                if (first) first = false;
                else out += STR(", ");
                out += '{';
                out += generate_property_value(this_struct, key_prop, key, implementation_file);
                out += STR(", ");
                out += generate_property_value(this_struct, value_prop, value, implementation_file);
                out += '}';
            }
            out += '}';
            return out;
        }
        if (auto prop = CastField<FMulticastSparseDelegateProperty>(property))
        {
            auto& value = prop->GetPropertyValue(data);
            if (!value.b_is_bound)
            {
                return STR("{}");
            }
            Output::send<LogLevel::Warning>(STR("Unhandled value of {}\n"), prop->GetFullName());
            return STR("{}");
        }
        if (auto prop = CastField<FMulticastInlineDelegateProperty>(property))
        {
            auto value = prop->GetMulticastDelegate(data);
            if (!value->InvocationList.Num())
            {
                return STR("{}");
            }
            Output::send<LogLevel::Warning>(STR("Unhandled value of {}\n"), prop->GetFullName());
            return STR("{}");
        }
        if (auto prop = CastField<FDelegateProperty>(property))
        {
            auto const& value = prop->GetPropertyValue(data);
            if (!value.IsBound())
            {
                return STR("{}");
            }
            Output::send<LogLevel::Warning>(STR("Unhandled value of {}\n"), prop->GetFullName());
            return STR("{}");
        }
        if (auto prop = CastField<FFieldPathProperty>(property))
        {
            auto const& value = prop->GetPropertyValue(data);
            if (!value.ResolvedField)
            {
                return STR("nullptr");
            }
            if (auto prop = CastField<FProperty>(value.ResolvedField))
            {
                return generate_find_property(prop, implementation_file);
            }
            Output::send<LogLevel::Warning>(STR("Unhandled value of {}\n"), prop->GetFullName());
            return STR("nullptr");
        }
        Output::send<LogLevel::Warning>(STR("Unhandled property {}\n"), property->GetFullName());
        return STR("{}");
    }
    auto UEHeaderGenerator::generate_struct_transform(UStruct* self, StringViewType native_name, FStructProperty* prop, void const* data, GeneratedSourceFile& file) -> StringType
    {
        auto& value = *const_cast<FTransform*>(static_cast<FTransform const*>(data));
        auto& rot = value.GetRotation();
        auto& trans = value.GetTranslation();
        auto& scale = value.GetScale3D();
        return fmt::format(STR("{}(FQuat({:.9e},{:.9e},{:.9e},{:.9e}), FVector({:.9e},{:.9e},{:.9e}), FVector({:.9e},{:.9e},{:.9e}))"),
                           native_name,
                           rot.GetX(),
                           rot.GetY(),
                           rot.GetZ(),
                           rot.GetW(),
                           trans.GetX(),
                           trans.GetY(),
                           trans.GetZ(),
                           scale.GetX(),
                           scale.GetY(),
                           scale.GetZ());
    }
    auto UEHeaderGenerator::generate_struct_soft_path(UStruct* self, StringViewType native_name, FStructProperty* prop, void const* data, GeneratedSourceFile& file) -> StringType
    {
        auto& value = *static_cast<FSoftObjectPath const*>(data);
        return generate_soft_path(native_name, value);
    }
    auto UEHeaderGenerator::generate_struct_frame_time(UStruct* self, StringViewType native_name, FStructProperty* prop, void const* data, GeneratedSourceFile& file) -> StringType
    {
        auto ustruct = prop->GetStruct();
        static auto prop_FrameNumber = CastField<FStructProperty>(ustruct->GetPropertyByName(STR("FrameNumber")));
        static auto prop_FrameNumber_Value = CastField<FIntProperty>(prop_FrameNumber->GetStruct()->GetPropertyByName(STR("Value")));
        static auto prop_SubFrame = CastField<FFloatProperty>(ustruct->GetPropertyByName(STR("SubFrame")));
        return fmt::format(STR("{}({}, {:.9e})"),
            native_name,
            prop_FrameNumber_Value->GetPropertyValueInContainer(prop_FrameNumber->ContainerPtrToValuePtr<void>(data)),
            prop_SubFrame->GetPropertyValueInContainer(data));
    }
    auto UEHeaderGenerator::generate_struct_gameplay_tag(UStruct* self, StringViewType native_name, FStructProperty* prop, void const* data, GeneratedSourceFile& file) -> StringType
    {
        auto ustruct = prop->GetStruct();
        static auto prop_TagName = CastField<FNameProperty>(ustruct->GetPropertyByName(STR("TagName")));
        //auto tag_name = generate_property_element_value(self, prop_TagName, prop_TagName->ContainerPtrToValuePtr<void>(data), file);
        auto tag_name = prop_TagName->GetPropertyValueInContainer(data);
        if (tag_name == NAME_None)
        {
            return fmt::format(STR("{}()"), native_name);
        }
        return fmt::format(STR("{}::RequestGameplayTag(TEXT(\"{}\"))"), native_name, tag_name.ToString());
    }

    auto UEHeaderGenerator::generate_env_query_test_work_on_float_values_element_assignment(UStruct* self, FProperty* property, void const* data, void const* arch_data, GeneratedSourceFile& file, PropertyScope& scope, bool write_defaults) -> void
    {
        auto value = generate_property_value(self, property, data, file);
        file.format_line(STR("SetWorkOnFloatValues({})"), value);
    }
    auto UEHeaderGenerator::struct_generators() -> std::unordered_map<FName, StructValueGenerator> const&
    {
        static std::unordered_map<FName, StructValueGenerator> generators;
        if (generators.empty())
        {
            generators.insert({FName(STR("Transform"), FNAME_Add), &generate_struct_transform});
            generators.insert({FName(STR("SoftObjectPath"), FNAME_Add), &generate_struct_soft_path});
            generators.insert({FName(STR("SoftClassPath"), FNAME_Add), &generate_struct_soft_path});
            generators.insert({FName(STR("FrameTime"), FNAME_Add), &generate_struct_frame_time});
            generators.insert({FName(STR("GameplayTag"), FNAME_Add), &generate_struct_gameplay_tag});
        }
        return generators;
    }
    auto UEHeaderGenerator::property_element_setters() -> std::unordered_map<FProperty*, PropertyElementSetter> const&
    {
        static std::unordered_map<FProperty*, PropertyElementSetter> setters;
        if (setters.empty())
        {
            auto EnvQueryTest = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/AIModule.EnvQueryTest"));
            auto EnvQueryTest_bWorkOnFloatValues = EnvQueryTest->GetPropertyByName(STR("bWorkOnFloatValues"));
            setters.insert({EnvQueryTest_bWorkOnFloatValues, &generate_env_query_test_work_on_float_values_element_assignment});
        }
        return setters;
    }
    auto UEHeaderGenerator::generate_default_property_value(UStruct* this_struct, FProperty* property, GeneratedSourceFile& header_data) -> StringType
    {
        if (property->GetArrayDim() == 1)
        {
            return generate_default_property_element_value(this_struct, property, header_data);
        }
        StringType out = STR("{");
        auto value = generate_default_property_element_value(this_struct, property, header_data);
        for (int32 i = 0; i < property->GetArrayDim(); ++i)
        {
            if (i != 0) out += STR(", ");
            out += value;
        }
        out += '}';
        return out;
    }
    auto UEHeaderGenerator::generate_default_property_element_value(UStruct* this_struct, FProperty* property, GeneratedSourceFile& header_data) -> StringType
    {
        if (auto prop = CastField<FStructProperty>(property))
        {
            auto data = get_default_object(prop->GetStruct());
            return generate_property_element_value(this_struct, property, data, header_data);
        }
        if (property->IsA<FArrayProperty>() || property->IsA<FSetProperty>() || property->IsA<FMapProperty>())
        {
            return STR("{}");
        }
        if (property->IsA<FTextProperty>())
        {
            return STR("FText::GetEmpty()");
        }
        char data[40] = {}; // SoftObjectPtr
        if (sizeof(data) < property->GetElementSize() || !(property->GetPropertyFlags() & CPF_ZeroConstructor))
        {
            Output::send<LogLevel::Warning>(STR("Incorrectly implemented generate_default_property_element_value for {} (size = {})\n"), property->GetFullName(), property->GetElementSize());
            return STR("{}");
        }
        return generate_property_element_value(this_struct, property, data, header_data);
    }

    auto UEHeaderGenerator::get_class_blueprint_info(UClass* uclass) -> ClassBlueprintInfo
    {
        // These 3 classes are "Intrinsically" blueprintable - they are blueprintable and BlueprintType themselves,
        // but this modifier is not passed through the class hierarchy like normally done
        // So to force generation of correct Blueprintable/BlueprintType statements, we report them as non-blueprint-types
        if (uclass == UObject::StaticClass() || uclass == UActorComponent::StaticClass() || uclass == USceneComponent::StaticClass())
        {
            return ClassBlueprintInfo();
        }

        ClassBlueprintInfo blueprint_info{};
        UClass* super_class = uclass->GetSuperClass();
        if (super_class != NULL)
        {
            blueprint_info = get_class_blueprint_info(super_class);
        }

        for (FProperty* property : uclass->ForEachProperty())
        {
            auto property_flags = property->GetPropertyFlags();

            if ((property_flags & CPF_BlueprintVisible) != 0)
            {
                blueprint_info.is_blueprint_type = true;
                break;
            }
        }

        for (UFunction* function : uclass->ForEachFunction())
        {
            auto function_flags = function->GetFunctionFlags();

            if ((function_flags & FUNC_BlueprintEvent) != 0)
            {
                blueprint_info.is_blueprintable = true;
                blueprint_info.is_blueprint_type = true;
                break;
            }
            if ((function_flags & FUNC_BlueprintCallable) != 0)
            {
                blueprint_info.is_blueprint_type = true;
            }
        }
        return blueprint_info;
    }

    auto UEHeaderGenerator::is_script_package(UPackage* package) -> bool
    {
        StringType package_name = package->GetName();
        return package_name.starts_with(STR("/Script/"));
    }
    auto UEHeaderGenerator::get_module_name_for_package(UPackage* package) -> StringType
    {
        assert(!package->GetOuterPrivate());
        StringType package_name = package->GetName();
        assert(package_name.starts_with(STR("/Script/")));
        package_name.erase(0, wcslen(STR("/Script/")));
        return package_name;
    }
    auto UEHeaderGenerator::get_package_name_for_module(StringType out) -> StringType
    {
        out.insert(0, STR("/Script/"));
        return out;
    }

    PropertyListView::PropertyListView(SettingsManager::PropertyList const& list)
    {
        for (auto item : list.each_item())
        {
            StringType path{item.object_path}; // unfortunate copy since StaticFindObject wants it to be NUL terminated
            auto object = UObjectGlobals::StaticFindObject<UStruct*>(nullptr, nullptr, path);
            if (!object)
            {
                Output::send<LogLevel::Error>(STR("Cannot find property owner {}\n"), item.object_path);
                continue;
            }
            if (item.operation == SettingsManager::PropertyOp::Wildcard)
            {
                wildcard.insert(object);
            }
            else
            {
                FName name{item.property_name, FNAME_Add};
                if (!object->FindProperty(name))
                {
                    Output::send<LogLevel::Error>(STR("Cannot find property {}.{}\n"), object->GetFullName(), item.property_name);
                    continue;
                }
                ops[object].insert({name, item.operation});
            }
        }
    }
    auto PropertyListView::includes(FProperty* prop) const -> bool
    {
        auto owner = Cast<UStruct>(prop->GetOutermostOwner());
        auto has_wildcard = wildcard.contains(owner);
        auto it = ops.find(owner);
        if (it != ops.end())
        {
            auto opit = it->second.find(prop->GetFName());
            if (opit != it->second.end())
            {
                return opit->second != SettingsManager::PropertyOp::Exclude;
            }
        }
        return has_wildcard;
    }

    UEHeaderGenerator::UEHeaderGenerator(const FFilePath& root_directory)
        : bind_widget(UE4SSProgram::settings_manager.UHTHeaderGenerator.BindWidget), ignore_default(UE4SSProgram::settings_manager.UHTHeaderGenerator.IgnoreDefault)
    {
        m_root_directory = root_directory;
        m_primary_module_name = determine_primary_game_module_name();
        m_primary_module = find_module(m_primary_module_name);

        // Force inclusion of Core and CoreUObject into all the generated module build files
        m_forced_module_dependencies.push_back(STR("Core"));
        m_forced_module_dependencies.push_back(STR("CoreUObject"));
        // TODO not optimal, but still needed for the majority of the cases
        m_forced_module_dependencies.push_back(STR("Engine"));
    }

    auto UEHeaderGenerator::find_module(StringType name) -> UPackage*
    {
        auto package_name = get_package_name_for_module(name);
        return UObjectGlobals::StaticFindObject<UPackage*>(nullptr, nullptr, package_name);
    }
    auto UEHeaderGenerator::ignore_module(StringType name) -> void
    {
        auto package = find_module(name);
        if (package) m_ignored_modules.insert(package);
    }
    auto UEHeaderGenerator::ignore_selected_modules() -> void
    {
        // Never generate CoreUObject, since it contains a lot of intrinsic classes and is generally always the same
        // Also skip engine initially because engine contents should not change much either

        // Skip "Engine" and "CoreUObject" if requested
        if (UE4SSProgram::settings_manager.UHTHeaderGenerator.IgnoreEngineAndCoreUObject ||
            UE4SSProgram::settings_manager.UHTHeaderGenerator.IgnoreAllCoreEngineModules)
        {
            ignore_module(STR("Engine"));
            ignore_module(STR("CoreUObject"));
        }

        // Skip all core engine packages if requested
        if (UE4SSProgram::settings_manager.UHTHeaderGenerator.IgnoreAllCoreEngineModules)
        {
            ignore_module(STR("ActorLayerUtilities"));
            ignore_module(STR("ActorSequence"));
            ignore_module(STR("AIModule"));
            ignore_module(STR("AndroidPermission"));
            ignore_module(STR("AnimationCore"));
            ignore_module(STR("AnimationSharing"));
            ignore_module(STR("AnimGraphRuntime"));
            ignore_module(STR("AppleImageUtils"));
            ignore_module(STR("ArchVisCharacter"));
            ignore_module(STR("AssetRegistry"));
            ignore_module(STR("AssetTags"));
            ignore_module(STR("AudioAnalyzer"));
            ignore_module(STR("AudioCapture"));
            ignore_module(STR("AudioExtensions"));
            ignore_module(STR("AudioMixer"));
            ignore_module(STR("AudioPlatformConfiguration"));
            ignore_module(STR("AudioSynesthesia"));
            ignore_module(STR("AugmentedReality"));
            ignore_module(STR("AutomationUtils"));
            ignore_module(STR("AvfMediaFactory"));
            ignore_module(STR("BuildPatchServices"));
            ignore_module(STR("CableComponent"));
            ignore_module(STR("Chaos"));
            ignore_module(STR("ChaosCloth"));
            ignore_module(STR("ChaosNiagara"));
            ignore_module(STR("ChaosSolvers"));
            ignore_module(STR("ChaosSolverEngine"));
            ignore_module(STR("CinematicCamera"));
            ignore_module(STR("ClothingSystemRuntimeCommon"));
            ignore_module(STR("ClothingSystemRuntimeInterface"));
            ignore_module(STR("ClothingSystemRuntimeNv"));
            ignore_module(STR("CustomMeshComponent"));
            ignore_module(STR("DatasmithContent"));
            ignore_module(STR("DeveloperSettings"));
            ignore_module(STR("EditableMesh"));
            ignore_module(STR("EngineMessages"));
            ignore_module(STR("EngineSettings"));
            ignore_module(STR("EyeTracker"));
            ignore_module(STR("FacialAnimation"));
            ignore_module(STR("FieldSystemCore"));
            ignore_module(STR("FieldSystemEngine"));
            ignore_module(STR("Foliage"));
            ignore_module(STR("GameplayTags"));
            ignore_module(STR("GameplayTasks"));
            ignore_module(STR("GeometryCache"));
            ignore_module(STR("GeometryCacheTracks"));
            ignore_module(STR("GeometryCollectionCore"));
            ignore_module(STR("GeometryCollectionSimulationCore"));
            ignore_module(STR("GeometryCollectionEngine"));
            ignore_module(STR("GeometryCollectionTracks"));
            ignore_module(STR("GooglePAD"));
            ignore_module(STR("HeadMountedDisplay"));
            ignore_module(STR("ImageWrapper"));
            ignore_module(STR("ImageWriteQueue"));
            ignore_module(STR("ImgMedia"));
            ignore_module(STR("ImgMediaFactory"));
            ignore_module(STR("InputCore"));
            ignore_module(STR("InteractiveToolsFramework"));
            ignore_module(STR("JsonUtilities"));
            ignore_module(STR("Landscape"));
            ignore_module(STR("LevelSequence"));
            ignore_module(STR("LightPropagationVolumeRuntime"));
            ignore_module(STR("LiveLinkInterface"));
            ignore_module(STR("LocationServicesBPLibrary"));
            ignore_module(STR("LuminRuntimeSettings"));
            ignore_module(STR("MagicLeap"));
            ignore_module(STR("MagicLeapAR"));
            ignore_module(STR("MagicLeapARPin"));
            ignore_module(STR("MagicLeapAudio"));
            ignore_module(STR("MagicLeapController"));
            ignore_module(STR("MagicLeapEyeTracker"));
            ignore_module(STR("MagicLeapHandMeshing"));
            ignore_module(STR("MagicLeapHandTracking"));
            ignore_module(STR("MagicLeapIdentity"));
            ignore_module(STR("MagicLeapImageTracker"));
            ignore_module(STR("MagicLeapLightEstimation"));
            ignore_module(STR("MagicLeapPlanes"));
            ignore_module(STR("MagicLeapPrivileges"));
            ignore_module(STR("MagicLeapSecureStorage"));
            ignore_module(STR("MagicLeapSharedWorld"));
            ignore_module(STR("MaterialShaderQualitySettings"));
            ignore_module(STR("MediaAssets"));
            ignore_module(STR("MediaCompositing"));
            ignore_module(STR("MediaUtils"));
            ignore_module(STR("MeshDescription"));
            ignore_module(STR("MobilePatchingUtils"));
            ignore_module(STR("MotoSynth"));
            ignore_module(STR("MoviePlayer"));
            ignore_module(STR("MovieScene"));
            ignore_module(STR("MovieSceneCapture"));
            ignore_module(STR("MovieSceneTracks"));
            ignore_module(STR("MRMesh"));
            ignore_module(STR("NavigationSystem"));
            ignore_module(STR("NetCore"));
            ignore_module(STR("Niagara"));
            ignore_module(STR("NiagaraAnimNotifies"));
            ignore_module(STR("NiagaraCore"));
            ignore_module(STR("NiagaraShader"));
            ignore_module(STR("OculusHMD"));
            ignore_module(STR("OculusInput"));
            ignore_module(STR("OculusMR"));
            ignore_module(STR("OnlineSubsystem"));
            ignore_module(STR("OnlineSubsystemUtils"));
            ignore_module(STR("Overlay"));
            ignore_module(STR("PacketHandler"));
            ignore_module(STR("Paper2D"));
            ignore_module(STR("PhysicsCore"));
            ignore_module(STR("PhysXVehicles"));
            ignore_module(STR("ProceduralMeshComponent"));
            ignore_module(STR("PropertyAccess"));
            ignore_module(STR("PropertyPath"));
            ignore_module(STR("Renderer"));
            ignore_module(STR("Serialization"));
            ignore_module(STR("SessionMessages"));
            ignore_module(STR("SignificanceManager"));
            ignore_module(STR("Slate"));
            ignore_module(STR("SlateCore"));
            ignore_module(STR("SoundFields"));
            ignore_module(STR("StaticMeshDescription"));
            ignore_module(STR("SteamVR"));
            ignore_module(STR("SteamVRInputDevice"));
            ignore_module(STR("Synthesis"));
            ignore_module(STR("TcpMessaging"));
            ignore_module(STR("TemplateSequence"));
            ignore_module(STR("TimeManagement"));
            ignore_module(STR("UdpMessaging"));
            ignore_module(STR("UMG"));
            ignore_module(STR("UObjectPlugin"));
            ignore_module(STR("VariantManagerContent"));
            ignore_module(STR("VectorVM"));
            ignore_module(STR("WmfMediaFactory"));
        }
    }

    auto UEHeaderGenerator::dump_native_packages() -> void
    {
        ignore_selected_modules();

        if (UE4SSProgram::settings_manager.UHTHeaderGenerator.MakeAllConfigsEngineConfig)
        {
            Output::send<LogLevel::Warning>(STR("MakeAllConfigsEngineConfig is deprecated\n"));
        }

        Output::send(STR("Cleaning up previously generated SDK (if one exists)\n"));
        if (std::filesystem::exists(m_root_directory))
        {
            std::filesystem::remove_all(m_root_directory);
        }

        Output::send(STR("Initializing native packages dump\n"));

        std::vector<UClass*> native_classes_to_dump;
        std::vector<UScriptStruct*> native_structs_to_dump;
        std::vector<UEnum*> native_enums_to_dump;
        std::vector<UFunction*> native_delegates_to_dump;

        Output::send(STR("Gathering native objects for dumping\n"));
        UObjectGlobals::ForEachUObject([&](void* raw_object, int32_t chunk_index, int32_t object_index) {
            auto object = static_cast<UObject*>(raw_object);

            if (auto uclass = Cast<UClass>(object))
            {
                if (uclass->HasAnyClassFlags(CLASS_Native))
                {
                    native_classes_to_dump.push_back(uclass);
                }
            }
            else if (auto ustruct = Cast<UScriptStruct>(object))
            {
                if (ustruct->HasAnyStructFlags(STRUCT_Native))
                {
                    native_structs_to_dump.push_back(ustruct);
                }
            }
            else if (auto uenum = Cast<UEnum>(object))
            {
                if (!object->IsA<UUserDefinedEnum>())
                {
                    native_enums_to_dump.push_back(uenum);
                }
            }
            else if (auto function = Cast<UFunction>(object); function && is_delegate_signature_function(function))
            {
                // We are looking for delegate signature functions located inside the native packages
                // When they are located directly on the top level, they will result in a separate header, otherwise they will
                // be included into their respective outer header
                if (auto package = Cast<UPackage>(function->GetOuterPrivate()); package && is_script_package(package))
                {
                    native_delegates_to_dump.push_back(function);
                }
            }
            return RC::LoopAction::Continue;
        });

        for (UFunction* f : native_delegates_to_dump)
        {
            preprocess_delegate_signature(f);
        }
        for (UClass* c : native_classes_to_dump)
        {
            preprocess_class(c);
        }
        for (UScriptStruct* s : native_structs_to_dump)
        {
            preprocess_struct(s);
        }

        Output::send(STR("Attempting to dump {} native classes\n"), native_classes_to_dump.size());

        for (UFunction* delegate_signature_function : native_delegates_to_dump)
        {
            // Output::send(STR("Dumping native delegate type {}\n"), global_delegate_signature->GetName());
            generate_object_description_file(delegate_signature_function);
        }

        for (UClass* class_to_dump : native_classes_to_dump)
        {
            // Output::send(STR("Dumping native class {}\n"), class_to_dump->GetName());
            generate_object_description_file(class_to_dump);
        }

        Output::send(STR("Attempting to dump {} native structs\n"), native_structs_to_dump.size());

        for (UScriptStruct* struct_to_dump : native_structs_to_dump)
        {
            // Output::send(STR("Dumping native struct {}\n"), struct_to_dump->GetName());
            generate_object_description_file(struct_to_dump);
        }

        Output::send(STR("Attempting to dump {} native enums\n"), native_enums_to_dump.size());

        for (UEnum* enum_to_dump : native_enums_to_dump)
        {
            // Output::send(STR("Dumping native enum {}\n"), enum_to_dump->GetName());
            generate_object_description_file(enum_to_dump);
        }

        Output::send(STR("Writing stub module build files for {} modules\n"), m_module_dependencies.size());
        for (const auto& [package,_] : m_module_dependencies)
        {
            auto module_name = get_module_name_for_package(package);
            if (module_name == m_primary_module_name) continue;

            generate_module_implementation_file(package, module_name, false);
            generate_module_build_file(package, module_name, false);
        }
        // always generate primary game module, even if it was never referenced
        generate_module_implementation_file(m_primary_module, m_primary_module_name, true);
        generate_module_build_file(m_primary_module, m_primary_module_name, true);

        Output::send(STR("Done!\n"));
    }

    auto UEHeaderGenerator::preprocess_delegate_signature(UFunction* sig) -> void
    {
        preprocess_function(sig);
    }
    auto UEHeaderGenerator::preprocess_cdo_property(FProperty* property, void const* all_data, DefaultSubobjects& dsos) -> void
    {
        char const* ptr = static_cast<char const*>(all_data);
        char const* end = ptr + property->GetSize();
        for (; ptr != end; ptr += property->GetElementSize())
        {
            auto data = static_cast<void const*>(ptr);
            if (auto prop = CastField<FObjectProperty>(property))
            {
                auto value = prop->GetPropertyValue(data);
                if (value && value->HasAnyFlags(RF_DefaultSubObject))
                {
                    dsos.insert_or_update(const_cast<UObject*>(value));
                }
            }
            if (auto prop = CastField<FArrayProperty>(property))
            {
                auto inner_prop = prop->GetInner();
                auto& value = *static_cast<FScriptArray const*>(data);
                for (auto d : each_item(value, inner_prop))
                {
                    preprocess_cdo_property(inner_prop, d, dsos);
                }
            }
            if (auto prop = CastField<FSetProperty>(property))
            {
                auto element_prop = prop->GetElementProp();
                auto& value = *static_cast<FScriptSet const*>(data);
                for (auto d : each_item(value, element_prop))
                {
                    preprocess_cdo_property(element_prop, d, dsos);
                }
            }
            if (auto prop = CastField<FMapProperty>(property))
            {
                auto key_prop = prop->GetKeyProp();
                auto value_prop = prop->GetValueProp();
                auto& value = *static_cast<FScriptSet const*>(data);
                for (auto [kd, vd] : each_item(value, key_prop, value_prop))
                {
                    preprocess_cdo_property(key_prop, kd, dsos);
                    preprocess_cdo_property(value_prop, vd, dsos);
                }
            }
        }
    }
    auto UEHeaderGenerator::preprocess_class(UClass* uclass) -> void
    {
        if (!uclass || m_default_subobjects.contains(uclass)) return;
        preprocess_struct(uclass);
        DefaultSubobjects dsos;
        if (auto super = uclass->GetSuperClass())
        {
            preprocess_class(super);
            for (auto [super_dso_name, _] : m_default_subobjects[super])
            {
                dsos.insert_or_update(super_dso_name, nullptr);
            }
        }
        UObject const* cdo = uclass->GetClassDefaultObject();
        for (auto c = uclass; c; c = c->GetSuperClass())
        {
            for (auto prop : c->ForEachProperty())
            {
                preprocess_cdo_property(prop, prop->ContainerPtrToValuePtr<void>(cdo), dsos);
            }
        }
        m_default_subobjects[uclass] = std::move(dsos);
    }
    auto UEHeaderGenerator::preprocess_script_struct(UScriptStruct* ustruct) -> void
    {
        preprocess_struct(ustruct);
    }
    auto UEHeaderGenerator::preprocess_struct(UStruct* ustruct) -> void
    {
        auto uscriptstruct = Cast<UScriptStruct>(ustruct);
        //for (; ustruct; ustruct = ustruct->GetSuperStruct())
        {
            for (auto prop : ustruct->ForEachProperty())
            {
                bool blueprint_visible = prop->HasAnyPropertyFlags(CPF_BlueprintVisible);
                preprocess_property(prop, blueprint_visible);
                if (blueprint_visible && uscriptstruct)
                {
                    m_blueprint_visible_structs.insert(uscriptstruct);
                }
            }
            for (auto func : ustruct->ForEachFunction())
            {
                preprocess_function(func);
            }
        }
    }
    auto UEHeaderGenerator::preprocess_function(UFunction* func) -> void
    {
        for (auto prop : func->ForEachProperty())
        {
            bool blueprint_visible = func->HasAnyFunctionFlags(FUNC_BlueprintCallable);
            preprocess_property(prop, blueprint_visible);
        }
    }
    auto UEHeaderGenerator::preprocess_property(FProperty* property, bool blueprint_visible) -> void
    {
        if (auto prop = CastField<FEnumProperty>(property))
        {
            auto uenum = prop->GetEnum();
            if (auto underlying = CastField<FNumericProperty>(prop->GetUnderlyingProperty()))
            {
                m_underlying_enum_props.insert({uenum, underlying});
            }
            else
            {
                Output::send<LogLevel::Error>(STR("Non-numeric underlying property for enum {}\n"), uenum->GetFullName());
            }
        }
        if (auto prop = CastField<FNumericProperty>(property))
        {
            if (auto uenum = prop->GetIntPropertyEnum())
            {
                if (blueprint_visible) m_blueprint_visible_enums.insert(uenum);
            }
        }
        if (auto prop = CastField<FArrayProperty>(property))
        {
            preprocess_property(prop->GetInner(), blueprint_visible);
        }
        if (auto prop = CastField<FSetProperty>(property))
        {
            preprocess_property(prop->GetElementProp(), blueprint_visible);
            preprocess_hashed_property(prop->GetElementProp());
        }
        if (auto prop = CastField<FMapProperty>(property))
        {
            preprocess_property(prop->GetKeyProp(), blueprint_visible);
            preprocess_hashed_property(prop->GetKeyProp());
            preprocess_property(prop->GetValueProp(), blueprint_visible);
        }
        if (auto prop = CastField<FStructProperty>(property))
        {
            if (blueprint_visible) m_blueprint_visible_structs.insert(prop->GetStruct());
        }
    }
    auto UEHeaderGenerator::preprocess_hashed_property(FProperty* property) -> void
    {
        if (auto prop = CastField<FStructProperty>(property))
        {
            m_structs_that_need_get_type_hash.insert(prop->GetStruct());
        }
    }

    auto UEHeaderGenerator::generate_object_description_file(UObject* object) -> bool
    {
        auto package = Cast<UPackage>(object->GetOutermost());
        assert(package);
        if (m_ignored_modules.contains(package)) return false;

        auto file_base_name = get_file_base_name_for_object(object);
        auto module_name = get_module_name_for_package(package);

        GeneratedSourceFile header_file = GeneratedSourceFile::create_source_file(m_root_directory, module_name, file_base_name, false, object);
        GeneratedSourceFile implementation_file = GeneratedSourceFile::create_source_file(m_root_directory, module_name, file_base_name, true, object, &header_file);

        if (auto uclass = Cast<UClass>(object))
        {
            if (uclass->IsChildOf<UInterface>())
            {
                generate_interface_definition(uclass, header_file);
            }
            else
            {
                generate_object_definition(uclass, header_file);
                generate_object_implementation(uclass, implementation_file);
            }
        }
        else if (auto script_struct = Cast<UScriptStruct>(object))
        {
            generate_struct_definition(script_struct, header_file);
            generate_struct_implementation(script_struct, implementation_file);
        }
        else if (auto uenum = Cast<UEnum>(object))
        {
            generate_enum_definition(uenum, header_file);
        }
        else if (auto function = Cast<UFunction>(object))
        {
            assert(is_delegate_signature_function(function));
            assert(function->GetOuterPrivate()->IsA<UPackage>());
            generate_global_delegate_declaration(function, NULL, header_file);
        }
        else
        {
            throw std::runtime_error(RC::fmt("Provided object %S is not of a supported type: %S", object->GetName().c_str(), object->GetClassPrivate()->GetName().c_str()));
        }

        auto& out_dependency_module_names = m_module_dependencies[package];
        if (!header_file.has_content_to_save()) return false;

        header_file.serialize_file_content_to_disk(*this);
        implementation_file.serialize_file_content_to_disk(*this);

        // Record module names used in the files
        header_file.coalesce_module_dependencies(out_dependency_module_names);
        implementation_file.coalesce_module_dependencies(out_dependency_module_names);
        return true;
    }

    auto UEHeaderGenerator::generate_object_pre_declaration(std::vector<StringType>& decls, UObject* object) -> void
    {
        if (auto uclass = Cast<UClass>(object))
        {
            if (uclass->IsChildOf<UInterface>())
            {
                decls.push_back(fmt::format(STR("class {};"), get_native_class_name(uclass, true)));
            }
            decls.push_back(fmt::format(STR("class {};"), get_native_class_name(uclass, false)));
        }
        else if (auto script_struct = Cast<UScriptStruct>(object))
        {
            decls.push_back(fmt::format(STR("struct {};"), get_native_struct_name(script_struct)));
        }
        else if (object->IsA<UEnum>())
        {
            // TODO do we want them? They're not that easy since we do not know enum types precisely in advance
            throw std::invalid_argument("Enum pre-declarations are not supported");
        }
        else
        {
            throw std::invalid_argument("Provided object is not of a supported type, should be UClass/UScriptStruct/UEnum");
        }
    }

    auto UEHeaderGenerator::get_file_base_name_for_object(UObject* object) -> StringType
    {
        assert(object && object->GetOuterPrivate()->IsA<UPackage>());
        StringType header_name;
        if (object->IsA<UClass>() || object->IsA<UScriptStruct>() || object->IsA<UEnum>())
        {
            // Class and struct headers follow the relevant object name
            // Enumerations usually have the E prefix which will be present in the header names
            // We do not strip it because there are some broken headers that do not follow that convention (e.g. funny Wwise)
            header_name = object->GetName();
        }
        else if (auto func = Cast<UFunction>(object))
        {
            assert(is_delegate_signature_function(func));
            // for a top-level delegate, remove the postfix and use the function name as the header name
            header_name = strip_delegate_signature_postfix(func);
        }
        else
        {
            throw std::runtime_error(RC::fmt("Unsupported dependency object type %S: %S", object->GetClassPrivate()->GetName().c_str(), object->GetName().c_str()));
        }

        auto& id = m_header_ids[object];
        if (id.is_invalid())
        {
            id = m_used_file_names[header_name].generate();
        }
        fmt::format_to(std::back_inserter(header_name), STR("{}"), id);
        return header_name;
    }

    auto UEHeaderGenerator::generate_global_delegate_declaration(UFunction* signature_function, UClass* delegate_class, GeneratedSourceFile& header_data) -> void
    {
        generate_delegate_type_declaration(signature_function, delegate_class, header_data);
    }

    auto UEHeaderGenerator::determine_primary_game_module_name() -> std::wstring
    {
        HMODULE primary_executable_module = GetModuleHandleW(NULL);
        wchar_t module_name_buffer[1024]{'\0'};
        GetModuleFileNameW(primary_executable_module, module_name_buffer, ARRAYSIZE(module_name_buffer));

        // Retrieve the filename from the full path, strip down the extension
        FFilePath root_executable_path((std::wstring(module_name_buffer)));
        std::wstring filename = root_executable_path.filename().replace_extension().wstring();

        // Remove the shipping file postfix
        StringViewType shipping_postfix = STR("-Win64-Shipping");
        if (filename.ends_with(shipping_postfix))
        {
            filename.erase(filename.length() - shipping_postfix.length());
        }
        return filename;
    }

    auto UEHeaderGenerator::generate_cross_module_include(UObject* object) -> StringType
    {
        auto package = Cast<UPackage>(object->GetOutermost());
        assert(package);
        auto module_name = get_module_name_for_package(package);
        auto object_name = object->GetName();
        auto fallback_name = get_file_base_name_for_object(object);
        return fmt::format(STR("//CROSS-MODULE INCLUDE V2: -ModuleName={} -ObjectName={} -FallbackName={}"), module_name, object_name, fallback_name);
    }

    GeneratedFile::GeneratedFile(const FFilePath& full_file_path)
    {
        m_full_file_path = full_file_path;
        m_file_base_name = full_file_path.filename().replace_extension().wstring();
        m_current_indent_count = 0;
    }

    auto GeneratedFile::append_line(StringViewType line) -> void
    {
        format_line(STR("{}"), line);
    }
    auto GeneratedFile::append_line() -> void
    {
        m_file_contents_buffer += '\n';
    }
    auto GeneratedFile::append_line_no_indent(StringViewType line) -> void
    {
        format_line_no_indent(STR("{}"), line);
    }

    auto GeneratedFile::begin_indent_level() -> void
    {
        ++m_current_indent_count;
    }
    auto GeneratedFile::end_indent_level() -> void
    {
        assert(m_current_indent_count > 0);
        --m_current_indent_count;
    }

    auto GeneratedFile::serialize_file_content_to_disk(UEHeaderGenerator& generator) -> bool
    {
        if (!has_content_to_save())
        {
            return false;
        }
        // TODO might be slow, maybe move it out into the header generator?
        std::filesystem::create_directories(m_full_file_path.parent_path());

        std::wofstream file_output_stream;
        file_output_stream.open(m_full_file_path);
        if (!file_output_stream.is_open())
        {
            throw std::runtime_error("Failed to open the header file");
        }
        generate_file_contents(file_output_stream, generator);
        file_output_stream.close();
        return true;
    }

    auto GeneratedFile::has_content_to_save() const -> bool
    {
        return !m_file_contents_buffer.empty();
    }
    auto GeneratedFile::generate_file_contents(std::wofstream& out, UEHeaderGenerator& generator) -> void
    {
        out << m_file_contents_buffer;
    }

    auto GeneratedSourceFile::create_source_file(const FFilePath& root_dir,
                                                 StringViewType module_name,
                                                 StringType const& base_name,
                                                 bool is_implementation_file,
                                                 UObject* object,
                                                 GeneratedSourceFile* header_file) -> GeneratedSourceFile
    {
        FFilePath path{root_dir};
        path /= module_name;
        path /= is_implementation_file ? STR("Private") : STR("Public");
        path /= base_name;
        path += is_implementation_file ? STR(".cpp") : STR(".h");
        return {path, is_implementation_file, object, header_file};
    }

    GeneratedSourceFile::GeneratedSourceFile(const FFilePath& file_path, bool is_implementation_file, UObject* object, GeneratedSourceFile* header_file)
        : GeneratedFile(file_path)
        , m_is_implementation_file(is_implementation_file)
        , m_object(object)
        , m_header_file(header_file)
    {
    }

    auto GeneratedSourceFile::add_extra_include(std::wstring included_file_name) -> void
    {
        m_extra_includes.insert(included_file_name);
    }

    static UObject* get_dependency_object_for(UObject* object)
    {
        /*if (auto func = Cast<UFunction>(object); func && !func->GetOuterPrivate()->IsA<UPackage>() && is_delegate_signature_function(func))
        {
            return func->GetOuterPrivate();
        }*/
        while (!object->GetOuterPrivate()->IsA<UPackage>())
        {
            object = object->GetOuterPrivate();
        }
        return object;
    }
    auto GeneratedSourceFile::add_dependency(UObject* object, DependencyLevel dependency_level) -> void
    {
        assert(dependency_level > DependencyLevel::NoDependency);

        auto topmost = get_dependency_object_for(object);
        if (topmost == get_object()) return;

        auto& level = m_dependencies[topmost];
        if (level >= dependency_level) return;
        level = dependency_level;

        add_module_dependency(Cast<UPackage>(topmost->GetOuterPrivate()));
    }
    auto GeneratedSourceFile::add_module_dependency(UPackage* package) -> void
    {
        assert(package);
        if (package == get_package()) return;
        m_module_dependencies.insert(package);
    }
    auto GeneratedSourceFile::coalesce_module_dependencies(ModuleDeps& out) -> void
    {
        std::unordered_set<UObject*> seen;
        for (auto [dep, level] : m_dependencies)
        {
            if (level < DependencyLevel::Include) continue;
            coalesce_object(dep, seen);
        }
        auto& out_deps = is_implementation() ? out.private_deps : out.public_deps;
        out_deps.insert(module_dependencies().begin(), module_dependencies().end());
    }
    auto GeneratedSourceFile::coalesce_object(UObject* object, std::unordered_set<UObject*>& seen) -> void
    {
        if (!object) return;
        if (!seen.insert(object).second) return;
        if (auto ustruct = Cast<UStruct>(object))
        {
            add_module_dependency(Cast<UPackage>(ustruct->GetOutermost()));
            for (auto prop : ustruct->ForEachProperty())
            {
                coalesce_property(prop, seen);
            }
            coalesce_object(ustruct->GetSuperStruct(), seen);
        }
    }
    auto GeneratedSourceFile::coalesce_property(FProperty* property, std::unordered_set<UObject*>& seen) -> void
    {
        if (auto prop = CastField<FStructProperty>(property))
        {
            coalesce_object(prop->GetStruct(), seen);
        }
        if (auto prop = CastField<FArrayProperty>(property))
        {
            coalesce_property(prop->GetInner(), seen);
        }
        if (auto prop = CastField<FSetProperty>(property))
        {
            coalesce_property(prop->GetElementProp(), seen);
        }
        if (auto prop = CastField<FMapProperty>(property))
        {
            coalesce_property(prop->GetKeyProp(), seen);
            coalesce_property(prop->GetValueProp(), seen);
        }
    }
    auto GeneratedSourceFile::generate_includes_string(StringType& out, UEHeaderGenerator& generator) const -> void
    {
        std::vector<StringType> local_includes;
        std::vector<StringType> cross_module_includes;
        std::vector<StringType> pre_decls;

        if (is_header())
        {
            // For the header file, we generate the pragma and minimal core includes
            out.append(STR("#pragma once\n"));
            out.append(STR("#include \"CoreMinimal.h\"\n"));
        }
        else
        {
            // For CPP implementation file, we need to generate the header include
            if (m_header_file)
            {
                // Generate it if we have the correct header file set
                fmt::format_to(std::back_inserter(out), STR("#include \"{}.h\"\n"), m_header_file->m_file_base_name);
            }
            else
            {
                // Otherwise, we generate a simple minimal core include
                out.append(STR("#include \"CoreMinimal.h\"\n"));
            }
        }

        // Generate extra includes we might need that do not represent objects
        for (auto const& extra_included_file : m_extra_includes)
        {
            local_includes.push_back(extra_included_file);
        }

        // Generate includes for the relevant object files
        for (auto [dep, level] : m_dependencies)
        {
            // Skip includes that have already been generated on the header file
            if (m_header_file && m_header_file->has_dependency(dep, level)) continue;

            if (level == DependencyLevel::Include)
            {
                auto dep_package = Cast<UPackage>(dep->GetOutermost());
                if (dep_package == get_package())
                {
                    // If this package corresponds to the file inside this module, we generate the normal include,
                    // since generated headers are always located in the module root and follow one file per object convention
                    local_includes.push_back(fmt::format(STR("{}.h"), generator.get_file_base_name_for_object(dep)));
                }
                else
                {
                    // Otherwise, we generate an include stub which will be handled by the unreal engine commandlet later
                    cross_module_includes.emplace_back(generator.generate_cross_module_include(dep));
                }
            }
            else if (level == DependencyLevel::PreDeclaration)
            {
                generator.generate_object_pre_declaration(pre_decls, dep);
            }
            else
            {
                assert(false);
            }
        }

        // Sort everything for determinism
        std::sort(cross_module_includes.begin(), cross_module_includes.end());
        std::sort(local_includes.begin(), local_includes.end());
        std::sort(pre_decls.begin(), pre_decls.end());

        for (auto const& incl : cross_module_includes)
        {
            fmt::format_to(std::back_inserter(out), STR("{}\n"), incl);
        }
        for (const auto& file : local_includes)
        {
            fmt::format_to(std::back_inserter(out), STR("#include \"{}\"\n"), file);
        }
        // Last include of the header file should always be a generated one
        if (!m_is_implementation_file)
        {
            fmt::format_to(std::back_inserter(out), STR("#include \"{}.generated.h\"\n"), m_file_base_name);
        }
        if (!pre_decls.empty())
        {
            if (!out.empty()) out += '\n';
            for (const auto& decl : pre_decls)
            {
                fmt::format_to(std::back_inserter(out), STR("{}\n"), decl);
            }
        }
        out += '\n';
    }

    auto GeneratedSourceFile::has_content_to_save() const -> bool
    {
        return !m_file_contents_buffer.empty();
    }
    auto GeneratedSourceFile::generate_file_contents(std::wofstream& out, UEHeaderGenerator& generator) -> void
    {

        StringType includes;
        generate_includes_string(includes, generator);
        out << includes;
        out << m_file_contents_buffer;
    }
    auto GeneratedSourceFile::has_dependency(UObject* object, DependencyLevel dependency_level) -> bool
    {
        auto it = m_dependencies.find(object);
        return it != m_dependencies.end() && it->second >= dependency_level;
    }
} // namespace RC::UEGenerator
