#include "pch.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Scripting/ScriptBindingGuard.hpp"

#include "Components/UI/LocalizedTextComponent.hpp"
#include "Localization/GameLocalization.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

// Localization for C# (the game's Assets/Localization tables) and the Localized Text component.
namespace Funke {
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);

	namespace {
		// Returns the bytes needed without the terminator; copies what fits.
		int CopyToBuffer(std::string_view value, char* outBuffer, int capacity) {
			const int required = static_cast<int>(std::min(value.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
			if (outBuffer && capacity > 0) {
				const int bytes = std::min(required, capacity - 1);
				if (bytes > 0) std::memcpy(outBuffer, value.data(), static_cast<size_t>(bytes));
				outBuffer[bytes] = '\0';
			}
			return required;
		}

		LocalizedTextComponent* FindLocalizedText(uint64_t entityID) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(entityID, scene, handle) || !scene->IsValid(handle)) return nullptr;
			return scene->GetRegistry().try_get<LocalizedTextComponent>(handle);
		}

		int Funke_Localization_GetBuffer(const char* key, char* outBuffer, int capacity) {
			if (!key || !*key) return CopyToBuffer({}, outBuffer, capacity);
			return CopyToBuffer(GameLocalization::Get(key), outBuffer, capacity);
		}

		int Funke_Localization_HasKey(const char* key) {
			return key && *key && GameLocalization::Has(key) ? 1 : 0;
		}

		int Funke_Localization_GetLanguageBuffer(int which, char* outBuffer, int capacity) {
			switch (which) {
			case 0: return CopyToBuffer(GameLocalization::GetCurrentLanguage(), outBuffer, capacity);
			case 1: return CopyToBuffer(GameLocalization::GetFallbackLanguage(), outBuffer, capacity);
			case 2: return CopyToBuffer(GameLocalization::GetSystemLanguage(), outBuffer, capacity);
			default: return CopyToBuffer({}, outBuffer, capacity);
			}
		}

		int Funke_Localization_SetLanguage(const char* code) {
			return code && *code && GameLocalization::SetLanguage(code) ? 1 : 0;
		}

		int Funke_Localization_GetLanguagesBuffer(char* outBuffer, int capacity) {
			Json::Value list = Json::Value::MakeArray();
			for (const GameLocalization::LanguageInfo& language : GameLocalization::GetAvailableLanguages()) {
				Json::Value entry = Json::Value::MakeObject();
				entry.AddMember("code", Json::Value(language.Code));
				entry.AddMember("name", Json::Value(language.DisplayName));
				list.Append(std::move(entry));
			}
			return CopyToBuffer(Json::Stringify(list, false), outBuffer, capacity);
		}

		uint32_t Funke_Localization_GetRevision() {
			return GameLocalization::GetRevision();
		}

		int Funke_LocalizedText_GetKeyBuffer(uint64_t entityID, char* outBuffer, int capacity) {
			const LocalizedTextComponent* localized = FindLocalizedText(entityID);
			return CopyToBuffer(localized ? std::string_view(localized->Key) : std::string_view{}, outBuffer, capacity);
		}

		void Funke_LocalizedText_SetKey(uint64_t entityID, const char* key) {
			if (LocalizedTextComponent* localized = FindLocalizedText(entityID)) localized->Key = key ? key : "";
		}
	}

	void PopulateLocalizationBindings(NativeBindings& b) {
		b.Localization_GetBuffer = FNK_GUARDED_BINDING(Funke_Localization_GetBuffer);
		b.Localization_HasKey = FNK_GUARDED_BINDING(Funke_Localization_HasKey);
		b.Localization_GetLanguageBuffer = FNK_GUARDED_BINDING(Funke_Localization_GetLanguageBuffer);
		b.Localization_SetLanguage = FNK_GUARDED_BINDING(Funke_Localization_SetLanguage);
		b.Localization_GetLanguagesBuffer = FNK_GUARDED_BINDING(Funke_Localization_GetLanguagesBuffer);
		b.Localization_GetRevision = FNK_GUARDED_BINDING(Funke_Localization_GetRevision);
		b.LocalizedText_GetKeyBuffer = FNK_GUARDED_BINDING(Funke_LocalizedText_GetKeyBuffer);
		b.LocalizedText_SetKey = FNK_GUARDED_BINDING(Funke_LocalizedText_SetKey);
	}

}
