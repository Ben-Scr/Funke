#pragma once

#include "Assets/AssetRegistry.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Scripting/Bolt/EntityScriptSource.hpp"
#include "Scripting/Bolt/InspectorSource.hpp"
#include "Scripting/Bolt/EmbeddedSources.hpp"
#include "Scripting/ScriptType.hpp"

namespace Funke::BoltScriptHost {
	struct ComponentUnit {
		uint64_t Guid;
		std::string Path, Name, Source;
		size_t Body;
		bool Managed = false;
		std::unordered_set<std::string> ReadOnly;
		ScriptBase Kind = ScriptBase::Component;
        uint32_t ManagedSize = 0;
        std::map<std::string,uint32_t> ManagedOffsets;
	};

	inline ComponentUnit SystemDeclaration(uint64_t guid, const std::string& path, const std::string& name, ScriptBase kind, bool managed = false, bool editorReference = false) {
		ComponentUnit unit{};
		unit.Guid = guid;
		unit.Name = name;
		unit.Path = path;
		unit.Kind = kind;
		unit.Managed = managed;
		const auto dot = name.rfind('.');
		if (dot != name.npos) unit.Source = "namespace " + name.substr(0, dot) + ";\n";
		unit.Source += "public class " + name.substr(dot == name.npos ? 0 : dot + 1) + " : Funke."
			+ (kind == ScriptBase::Scene ? "SceneSystem" : "GlobalSystem");
		if (editorReference) unit.Source += " {}";
		else unit.Source += " { public override string FkSystemName() { return \""
			+ (managed ? "type:" + name : std::to_string(guid)) + "\"; } }";
		return unit;
	}

	inline std::vector<ComponentUnit> s_ManagedComponentUnits;

	inline bool ReadManagedComponentSchema(const std::string& json, std::vector<ComponentUnit>& units, bool editorReference = false) {
		Json::Value catalog;
		if (!Json::TryParse(json, catalog) || !catalog.IsArray()) return false;
		for (const auto& item : catalog.GetArray()) {
			const auto* name = item.FindMember("name");
			const auto* fields = item.FindMember("fields");
			if (!name || !name->IsString() || !fields || !fields->IsArray()) return false;
			ComponentUnit unit{};
			unit.Managed = true;
			unit.Name = name->AsStringOr();
			unit.Path = "<CSharp>/" + unit.Name + ".bolt";
			if (const auto* kind = item.FindMember("kind"); kind && kind->AsStringOr() != "component") {
				if (kind->AsStringOr()=="nativeStruct") {
                    unit.Kind=ScriptBase::NativeStruct;
                    const auto* size=item.FindMember("size");
                    if (!size || !size->AsUInt64Or(0) || size->AsUInt64Or(0)>UINT32_MAX) return false;
                    unit.ManagedSize=static_cast<uint32_t>(size->AsUInt64Or(0));
                    auto dot=unit.Name.rfind('.');
                    if(dot!=unit.Name.npos) unit.Source="namespace "+unit.Name.substr(0,dot)+";\n";
                    unit.Source+="public struct "+unit.Name.substr(dot==unit.Name.npos?0:dot+1)+" : Funke.Components.IComponent {";
                    unit.Body=unit.Source.size();
                    for(const auto& field:fields->GetArray()) {
                        auto key=field.FindMember("name"), type=field.FindMember("type"), offset=field.FindMember("offset"), visible=field.FindMember("visible");
                        if(!key || !type || !offset || !visible || offset->AsUInt64Or(UINT64_MAX)>=unit.ManagedSize) return false;
                        unit.ManagedOffsets.emplace(key->AsStringOr(),static_cast<uint32_t>(offset->AsUInt64Or(0)));
                        unit.Source+=(visible->AsBoolOr(false)?" public ":" private ")+type->AsStringOr()+" "+key->AsStringOr()+";";
                    }
                    unit.Source+="}";
                    units.push_back(std::move(unit)); continue;
                }
                units.push_back(SystemDeclaration(0, unit.Path, unit.Name,
					kind->AsStringOr() == "sceneSystem" ? ScriptBase::Scene : ScriptBase::Global, true, editorReference));
				continue;
			}
			unit.Source = "import Funke\n";
			const auto dot = unit.Name.rfind('.');
			if (dot != unit.Name.npos) unit.Source += "namespace " + unit.Name.substr(0, dot) + ";\n";
			unit.Source += "public class " + unit.Name.substr(dot == unit.Name.npos ? 0 : dot + 1) + " : Funke.Component {";
			unit.Body = unit.Source.size();
			for (const auto& field : fields->GetArray()) {
				const auto* type = field.FindMember("type");
				const auto* member = field.FindMember("name");
				const auto* writable = field.FindMember("writable");
				if (!type || !member || !writable) return false;
				const auto fieldType = type->AsStringOr();
				if (editorReference) {
					const bool token = fieldType == "token";
					const bool task = fieldType.starts_with("task:");
					const auto typeName = token ? std::string("System.Threading.CancellationToken") :
						task ? "System.Threading.Tasks.Task<" + fieldType.substr(5) + ">" : fieldType;
					unit.Source += "\n    public " + typeName + " " + member->AsStringOr() +
						(writable->AsBoolOr(false) && !task && !token ? " { get; set; }" : " { get; }");
					continue;
				}
                if(fieldType=="token") {
                    unit.Source+="\n public System.Threading.CancellationToken "+member->AsStringOr()
                        +" { get { return Funke.ManagedTasks.OpenToken(ID,\""+unit.Name+"."+member->AsStringOr()+"\"); } }";
                    continue;
                }
                if (fieldType.starts_with("task:")) {
                    auto result=fieldType.substr(5);
                    auto getter=result=="void" ? std::string("OpenVoid") : "Open<"+result+">";
                    const auto decode=result=="void" ? std::string{} : ",Funke.ManagedTasks.Decode"+result;
                    unit.Source+="\n public System.Threading.Tasks.Task<"+result+"> "+member->AsStringOr()
                        +" { get { return Funke.ManagedTasks."+getter+"(ID,\""+unit.Name+"."+member->AsStringOr()+"\""+decode+"); } }";
                    continue;
                }
                unit.Source += "\n    public " + type->AsStringOr() + " " + member->AsStringOr() + ";";
				if (!writable->AsBoolOr(false)) unit.ReadOnly.insert(member->AsStringOr());
			}
			unit.Source += "\n}\n";
			units.push_back(std::move(unit));
		}
		return true;
	}

	using ScriptFileStamps = std::unordered_map<std::string, std::pair<std::string, std::filesystem::file_time_type>>;

	inline std::vector<ComponentUnit> ComponentUnits(ScriptFileStamps* stamps = nullptr) {
		struct Cached {
			std::filesystem::file_time_type Modified;
			std::string Path;
			std::optional<ComponentUnit> Unit;
		};
		static thread_local std::unordered_map<uint64_t,Cached> cache;
		std::vector<ComponentUnit> units;
		std::unordered_set<uint64_t> seen;
		for (const auto& asset : AssetRegistry::GetAssetsByKind(AssetKind::Script)) {
			const auto path = Path::FromUtf8(asset.Path);
			if (!ScriptTypeEqualsIgnoreCase(path.extension().string(), ".bolt")) continue;
			seen.insert(asset.Id);
			std::error_code ec;
			auto modified = std::filesystem::last_write_time(path, ec);
			if (stamps && !ec) stamps->emplace(std::to_string(asset.Id), std::make_pair(asset.Path, modified));
			auto& cached = cache[asset.Id];
			if (cached.Path != asset.Path || cached.Modified != modified || ec) {
				cached = {modified, asset.Path, {}};
				auto source = File::ReadAllText(asset.Path);
				ScriptBase kind = ScriptBase::Procedural;
				size_t body = 0;
				auto name = ScriptClass(source, kind, &body);
				if (kind == ScriptBase::Component || kind == ScriptBase::NativeStruct || kind==ScriptBase::Job || kind==ScriptBase::DataAsset || kind==ScriptBase::EnumLibrary)
                    cached.Unit.emplace(ComponentUnit{asset.Id, asset.Path, std::move(name), std::move(source), body, false, {}, kind});
				else if (kind == ScriptBase::Scene || kind == ScriptBase::Global)
					cached.Unit = SystemDeclaration(asset.Id, asset.Path, name, kind);
			}
			if (cached.Unit) units.push_back(*cached.Unit);
		}
		std::erase_if(cache,[&](const auto& item) { return !seen.contains(item.first); });
		std::sort(units.begin(),units.end(),[](const auto& a,const auto& b){return a.Guid<b.Guid;});
		units.insert(units.end(), s_ManagedComponentUnits.begin(), s_ManagedComponentUnits.end());
		return units;
	}

    inline void AddDataEnums(FieldList& list, const std::vector<ComponentUnit>& units, const std::string& source) {
        static const auto sdkEnums = [] {
            DataEnums result;
            CollectDataEnums(EmbeddedSources::Core, result, "Funke");
            CollectDataEnums(EmbeddedSources::UI, result, "Funke.UI");
            CollectDataEnums(EmbeddedSources::Jobs, result, "Funke.Jobs");
            for (auto* module = EmbeddedSources::ExtraModules; module->Filename; ++module)
                CollectDataEnums(std::string(module->Preamble) + module->Body, result);
            return result;
        }();
        list.Enums = sdkEnums;
        for (const auto& unit : units)
            if (unit.Source.find("enum") != unit.Source.npos) CollectDataEnums(unit.Source, list.Enums);
        CollectDataEnums(source, list.Enums);
        std::string_view imports(source);
        while (!imports.empty()) {
            if (NextBoltToken(imports) != "import") continue;
            const auto end = imports.find_first_of("\r\n");
            auto line = imports.substr(0, end);
            std::string name;
            for (auto part = NextBoltToken(line); !part.empty(); part = NextBoltToken(line)) name += part;
            list.Enums.Imports.push_back(std::move(name));
            imports.remove_prefix(end == imports.npos ? imports.size() : end);
        }
    }

	inline void AddReferences(FieldList& list, const std::vector<ComponentUnit>& units) {
		std::unordered_map<std::string, size_t> counts;
		for (const auto& unit : units) {
			if (unit.Kind != ScriptBase::Component && unit.Kind != ScriptBase::DataAsset) continue;
			list.References.emplace(unit.Name, unit.Kind == ScriptBase::DataAsset ? "dataasset:bolt:" + std::to_string(unit.Guid) : unit.Managed ? "type:" + unit.Name : unit.Name);
			const auto dot = unit.Name.rfind('.');
			if (dot != std::string::npos) ++counts[unit.Name.substr(dot + 1)];
		}
		const auto ownerDot = list.Owner.rfind('.');
		for (const auto& unit : units) {
			if (unit.Kind != ScriptBase::Component && unit.Kind != ScriptBase::DataAsset) continue;
			const auto dot = unit.Name.rfind('.');
			if (dot == std::string::npos) continue;
			const auto localName = unit.Name.substr(dot + 1);
			const bool sameNamespace = ownerDot != std::string::npos && unit.Name.substr(0, dot) == list.Owner.substr(0, ownerDot);
			const auto identity = unit.Kind == ScriptBase::DataAsset ? "dataasset:bolt:" + std::to_string(unit.Guid) : unit.Managed ? "type:" + unit.Name : unit.Name;
			if (sameNamespace) list.References[localName] = identity;
			else if (counts[localName] == 1) list.References.emplace(localName, identity);
		}
	}

	inline void BindNativeQueryKeys(std::string& source, bool managed) {
		if (source.find("IComponent") == std::string::npos) return;
		const auto declarations = ScriptDeclarations(source);
		for (auto it = declarations.rbegin(); it != declarations.rend(); ++it) {
			if (it->Kind != ScriptBase::NativeStruct) continue;
			const std::string key = (managed ? "type:" : "bolt:") + it->Name;
			source.insert(it->Body, " public string FkQueryKey() { return \"" + key + "\"; } ");
		}
	}

	inline bool BindComponent(ComponentUnit& unit, const FieldList& schema, std::string& error) {
		const bool dataAsset = unit.Kind == ScriptBase::DataAsset;
        const auto& references = schema.References;
        std::string describe = " public void FkDescribeEnums() {";
        if (unit.Kind != ScriptBase::Component && !dataAsset) return true;
		const auto guid = std::to_string(unit.Guid);
		const std::string id = guid + "ul";
		std::string methods = " public override string FkQueryKey() {return \"#" + guid + "\";}"
			" public override bool FkHas(ulong e) {return Funke.FunkeComponentStorage.Has(e," + id + ");}"
			" public override bool FkAdd(ulong e) {return Funke.FunkeComponentStorage.Add(e," + id + ");}"
			" public override bool FkRemove(ulong e) {return Funke.FunkeComponentStorage.Remove(e," + id + ");} ";
        if (dataAsset) methods = " public override ulong FkDataType() { return " + id + "; } ";
		if (unit.Managed) {
			const std::string typeName = "\"" + unit.Name + "\"";
			methods = " public override string FkQueryKey() {return \"@" + unit.Name + "\";}"
				" public override bool FkHas(ulong e) {return Funke.FunkeManagedStorage.Has(e," + typeName + ");}"
				" public override bool FkAdd(ulong e) {return Funke.FunkeManagedStorage.Add(e," + typeName + ");}"
				" public override bool FkRemove(ulong e) {return Funke.FunkeManagedStorage.Remove(e," + typeName + ");} ";
		}
		std::string_view rest(unit.Source);
		rest.remove_prefix(unit.Body);
		std::string output = unit.Source.substr(0, unit.Body) + methods;
		const char* last = rest.data();
        const char* classEnd = nullptr;
		size_t depth = 0;
		bool local = false;
		while (!rest.empty()) {
			const auto t = NextBoltToken(rest);
			if (t.empty()) break;
			if (t == "{") { ++depth; continue; }
			if (t == "}") { if (!depth) { classEnd = t.data(); break; } --depth; local = false; continue; }
			if (depth) continue;
			if (t == ";") { local = false; continue; }
			if (t == "static" || t == "const") { local = true; continue; }
			if (local || !(std::isalpha(static_cast<unsigned char>(t.front())) || t.front() == '_')) continue;
			auto tail = rest;
			auto field = NextBoltToken(tail);
			std::string type(t);
			while (field == ".") {
				type += "." + std::string(NextBoltToken(tail));
				field = NextBoltToken(tail);
			}
			const bool optional = field == "?";
			if (optional) field = NextBoltToken(tail);
			auto delimiter = NextBoltToken(tail);
			if (field.empty() || !std::isalpha(static_cast<unsigned char>(field.front())) && field.front() != '_'
				|| (delimiter != "=" && delimiter != ";")) continue;
			std::string initial;
			if (delimiter == "=") {
				const auto* start = tail.data();
				while (!(delimiter = NextBoltToken(tail)).empty() && delimiter != ";") {}
				if (delimiter.empty()) { error = unit.Path + ": unterminated component field"; return false; }
				initial.assign(start, delimiter.data());
			}
			const std::string key = "\"" + (dataAsset ? std::string{} : (unit.Managed ? unit.Name : guid) + ".") + std::string(field) + "\"";
			static constexpr std::string_view numbers[] = {"sbyte","byte","short","ushort","int","uint","long","ulong","float","double"};
			const bool number = std::find(std::begin(numbers),std::end(numbers),type) != std::end(numbers);
			const auto& name = type.starts_with("Funke.") ? type.substr(6) : type;
			const auto channels = FieldChannels(name);
			const bool builtin = type.starts_with("Funke.");
			const auto& known = builtin ? BuiltInFieldReferences() : references;
			auto reference = known.find(name);
			if (reference == known.end() && builtin && name.starts_with("UI."))
				reference = known.find(name.substr(3));
            const auto assetTag = AssetFieldTag(type, references);
			const bool entity = name == "Entity";
			std::string read, write = "(string)value";
			const auto* enumType = dataAsset ? DataEnumFor(schema.Enums, unit.Name, type) : nullptr;
            if (enumType && !optional) {
                const auto& integer = enumType->Underlying;
                read = integer + " parsed; if (" + integer + ".TryParse(raw.Value,parsed)) return (" + type + ")parsed;";
                write = "(string)(" + integer + ")value";
                const auto member = std::string(field);
                describe += "Funke.FkDataStorage.Write(FkGuid," + key + ",(string)(" + integer + ")this." + member + ");";
                for (const auto& item : enumType->Members)
                    describe += "Funke.FkDataStorage.EnumOption(FkGuid," + key + ",\"" + item + "\",(string)(" +
                        integer + ")" + type + "." + item + ");";
            } else if (number) read = type + " result; if (" + type + ".TryParse(raw.Value,result)) return result;";
			else if (name == "bool") read = "return raw.Value == \"true\" || raw.Value == \"True\";";
			else if (name == "string") read = "return raw.Value;";
			else if (!channels.empty()) {
				read = "string[] parts=raw.Value.Split(','); if(parts.Length==" + std::to_string(channels.size()) + ") {";
				std::string args, check;
				write.clear();
				for (size_t i=0; i<channels.size(); ++i) {
					std::string v(1,channels[i]);
					read += "float v"+v+";";
					if (i) { check+=" && "; args+=","; write+=" + \",\" + "; }
					check += "float.TryParse(parts["+std::to_string(i)+"],v"+v+")";
					args += "v"+v;
					write += "(string)value."+v;
				}
				read += "if ("+check+") return "+type+"("+args+");}";
				if (initial.empty()) initial=type+"()";
			} else if (!assetTag.empty()) {
                read = "ulong guid = 0ul; if(ulong.TryParse(raw.Value,guid)) {"+type+"? found=Funke.AssetManager.LoadByGUID<"+type+">(guid);";
                read += optional ? "return found;}" : "if(found!=null)return found.Value;}";
                write = "(string)value" + std::string(optional ? ".Value" : "") + (assetTag.starts_with("dataasset:") ? ".FkAssetId()" : ".UUID");
                if (optional) write = "(value==null ? \"0\" : "+write+")";
                if (initial.empty()) initial = optional ? "null" : type + "()";
            } else if (entity || reference != known.end()) {
				const std::string tag = entity ? "" : reference->second;
				read = "ulong target=Funke.FunkeInspector.ReferenceId(raw.Value,\""+tag+"\");";
				const auto get = entity ? "Funke.Entity.FromId(target)" : "Funke.Entity(target).GetComponent<"+type+">()";
				read += optional ? "return "+get+";" : type+"? found="+get+"; if(found!=null)return found.Value;";
				write = "(string)value"+std::string(optional ? ".Value" : "")+".ID";
				if (!entity) write+=" + \":"+tag+"\"";
				if (optional) write="(value==null ? \"\" : "+write+")";
				if (initial.empty()) initial=optional ? "null" : type+"()";
			} else {
				error = unit.Path + ": unsupported stored Bolt component field type '" + type + "'";
				return false;
			}
			if (optional && assetTag.empty() && !entity && reference == known.end()) {
				error = unit.Path + ": optional component data requires an entity or component reference";
				return false;
			}
			if (initial.empty()) initial = name == "string" ? "\"\"" : name == "bool" ? "false" : "(" + type + ")0";
			output.append(last, t.data());
			const std::string storage = dataAsset ? "Funke.FkDataStorage." : unit.Managed ? "Funke.FunkeManagedStorage." : "Funke.FunkeComponentStorage.";
			output += type + (optional ? "? " : " ") + std::string(field)
				+ " { get { string? raw = " + storage + "Read(" + (dataAsset ? "FkGuid," : "ID,") + key + "); if(raw!=null) {" + read + "}"
				+ "return " + initial + "; }";
			if (!unit.ReadOnly.contains(std::string(field)))
				output += " set { " + storage + "Write(" + (dataAsset ? "FkGuid," : "ID,") + key + "," + write + "); }";
			output += " }";
			rest = tail;
			last = rest.data();
		}
		if (dataAsset) {
            if (!classEnd) { error = unit.Path + ": unterminated DataAsset class"; return false; }
            output.append(last, classEnd);
            output += describe + "} ";
            last = classEnd;
        }
        output.append(last, static_cast<const char*>(unit.Source.data()) + unit.Source.size());
		unit.Source = std::move(output);
		return true;
	}
}
