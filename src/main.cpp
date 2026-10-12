// Ride Along - SKSE plugin: your follower rides behind you on your horse.
// Copyright (C) 2026 Clyde
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License (LICENSE)
// for more details. Built on CommonLibSSE-NG (GPL-3.0-or-later with exceptions).

// Ride Along - a follower rides behind you on your horse.
// The passenger is pinned to the horse's saddle every frame, right after the game has moved the horse and
// before the frame is drawn (in the camera update), so it cannot fall behind. Its character controller is
// detached while it rides, so it has no physics to fight the horse with.

#include <atomic>
#include <chrono>
#include <thread>
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "SKSEMenuFramework.h"  // optional in-game settings page (SKSE Menu Framework); does nothing when it isn't installed

#ifdef GetObject
#	undef GetObject  // windows.h renames it GetObjectA, which breaks BGSDefaultObjectManager::GetObject
#endif

namespace logger = SKSE::log;
using namespace std::literals;

namespace RideAlong
{
	// ------------------------------------------------------------------ settings
	struct Settings
	{
		std::uint32_t hotkey{ 0x25 };              // DirectX scan code, 0x25 = K
		float         back{ 34.0f };               // how far behind the saddle
		float         up{ -4.0f };                 // height relative to the saddle
		float         side{ 0.0f };                // + = to the horse's right
		float         range{ 1500.0f };            // how close a follower must be
		std::string   anchorNode{ "SaddleBone" };  // horse skeleton node the passenger is pinned to
		std::string   poseEvent{ "HorseEnterInstant" };
		std::string   exitEvent{ "IdleForceDefaultState" };
		bool          noCombat{ true };  // the passenger stays out of fights while she rides (her AI stays on: you can still talk to her)
		std::string   syncVars{ "Speed,Direction,TurnDelta,SpeedSampled,IsSprinting,bIsSprinting,iSyncSprintState,fSpeedMult,HorseSpeed,TurnDeltaDamped,SpeedDamped" };
		bool          dumpGraphVars{ false };
		bool          copyPose{ true };
		std::string   copyBones{ "Thigh,Calf,Foot,Toe,Pelvis" };
		std::string   skipBones{ "Head,Neck,Eye,Jaw,Tongue,Brow,Lip,Cheek,Mouth,Root,Weapon,Shield,Quiver,Sheath,Magic,Camera,Saddle,Hair" };
		std::string   moveBones{ "" };
		// 0.2: dialogue, controller, menu, dismount, princess seat
		bool          hotkeyEnabled{ true };
		std::uint32_t padButton{ 0 };              // controller button (0 = none); see the list in RideAlong.ini
		float         padHold{ 0.6f };             // seconds the controller button must be held
		bool          dialogueEnabled{ true };     // "Ride with me." / "You can get down now." (needs RideAlong.esp)
		bool          dismountAnim{ true };        // play a dismount animation before stepping off
		std::string   dismountEvent{ "HorseExit" };
		float         dismountSeconds{ 1.2f };
		bool          princess{ false };           // female passengers sit sideways (needs an OAR animation, see the readme)
		bool          princessCopyPose{ false };
		float         pBack{ 30.0f };
		float         pUp{ -2.0f };
		float         pSide{ 0.0f };
		float         pTurn{ 90.0f };              // degrees the passenger is turned on the seat (+ = facing the horse's right)
		std::string   pDismountEvent{ "" };        // empty = DismountEvent
	};
	Settings g;

	std::string Trim(const std::string& s)
	{
		const auto b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) {
			return {};
		}
		const auto e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	void LoadSettings()
	{
		std::ifstream in("Data/SKSE/Plugins/RideAlong.ini");
		if (!in) {
			logger::info("RideAlong.ini not found - using defaults");
			return;
		}
		std::string line;
		while (std::getline(in, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') {
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = Trim(line.substr(0, eq));
			auto       val = Trim(line.substr(eq + 1));
			if (const auto sc = val.find(';'); sc != std::string::npos) {
				val = Trim(val.substr(0, sc));
			}
			try {
				if (key == "Hotkey") {
					g.hotkey = static_cast<std::uint32_t>(std::stoul(val, nullptr, 0));
				} else if (key == "BackOffset") {
					g.back = std::stof(val);
				} else if (key == "UpOffset") {
					g.up = std::stof(val);
				} else if (key == "SideOffset") {
					g.side = std::stof(val);
				} else if (key == "Range") {
					g.range = std::stof(val);
				} else if (key == "AnchorNode") {
					g.anchorNode = val;
				} else if (key == "PoseEvent") {
					g.poseEvent = val;
				} else if (key == "ExitEvent") {
					g.exitEvent = val;
				} else if (key == "SyncVariables") {
					g.syncVars = val;
				} else if (key == "CopyRiderPose") {
					g.copyPose = std::stoi(val) != 0;
				} else if (key == "CopyBones") {
					g.copyBones = val;
				} else if (key == "SkipBones") {
					g.skipBones = val;
				} else if (key == "MoveBones") {
					g.moveBones = val;
				} else if (key == "DumpGraphVariables") {
					g.dumpGraphVars = std::stoi(val) != 0;
				} else if (key == "PeacefulPassenger") {
					g.noCombat = std::stoi(val) != 0;
				} else if (key == "HotkeyEnabled") {
					g.hotkeyEnabled = std::stoi(val) != 0;
				} else if (key == "GamepadButton") {
					g.padButton = static_cast<std::uint32_t>(std::stoul(val, nullptr, 0));
				} else if (key == "GamepadHoldSeconds") {
					g.padHold = std::stof(val);
				} else if (key == "DialogueEnabled") {
					g.dialogueEnabled = std::stoi(val) != 0;
				} else if (key == "DismountAnimation") {
					g.dismountAnim = std::stoi(val) != 0;
				} else if (key == "DismountEvent") {
					g.dismountEvent = val;
				} else if (key == "DismountSeconds") {
					g.dismountSeconds = std::stof(val);
				} else if (key == "PrincessSeat") {
					g.princess = std::stoi(val) != 0;
				} else if (key == "PrincessCopyPose") {
					g.princessCopyPose = std::stoi(val) != 0;
				} else if (key == "PrincessBackOffset") {
					g.pBack = std::stof(val);
				} else if (key == "PrincessUpOffset") {
					g.pUp = std::stof(val);
				} else if (key == "PrincessSideOffset") {
					g.pSide = std::stof(val);
				} else if (key == "PrincessTurnDegrees") {
					g.pTurn = std::stof(val);
				} else if (key == "PrincessDismountEvent") {
					g.pDismountEvent = val;
				}
			} catch (...) {
				logger::warn("RideAlong.ini: bad value for {}: '{}'", key, val);
			}
		}
		logger::info("sync variables: {}", g.syncVars);
		logger::info("settings: hotkey=0x{:X} back={} up={} side={} range={} anchor={} pose={} exit={} peaceful={}",
			g.hotkey, g.back, g.up, g.side, g.range, g.anchorNode, g.poseEvent, g.exitEvent, g.noCombat);
		logger::info("settings: hotkeyOn={} pad=0x{:X} hold={} dialogue={} dismountAnim={} ({} {}s) princess={} ({} {} {} turn {})",
			g.hotkeyEnabled, g.padButton, g.padHold, g.dialogueEnabled, g.dismountAnim, g.dismountEvent, g.dismountSeconds,
			g.princess, g.pBack, g.pUp, g.pSide, g.pTurn);
	}

	// Writes the settings the in-game menu can change back into RideAlong.ini, keeping every comment and line order.
	void SaveSettings()
	{
		const std::map<std::string, std::string> values{
			{ "Hotkey", std::format("0x{:X}", g.hotkey) },
			{ "HotkeyEnabled", g.hotkeyEnabled ? "1" : "0" },
			{ "GamepadButton", std::format("0x{:X}", g.padButton) },
			{ "GamepadHoldSeconds", std::format("{:.2f}", g.padHold) },
			{ "DialogueEnabled", g.dialogueEnabled ? "1" : "0" },
			{ "Range", std::format("{:.0f}", g.range) },
			{ "PeacefulPassenger", g.noCombat ? "1" : "0" },
			{ "BackOffset", std::format("{:.1f}", g.back) },
			{ "UpOffset", std::format("{:.1f}", g.up) },
			{ "SideOffset", std::format("{:.1f}", g.side) },
			{ "DismountAnimation", g.dismountAnim ? "1" : "0" },
			{ "DismountSeconds", std::format("{:.2f}", g.dismountSeconds) },
			{ "PrincessSeat", g.princess ? "1" : "0" },
			{ "PrincessCopyPose", g.princessCopyPose ? "1" : "0" },
			{ "PrincessBackOffset", std::format("{:.1f}", g.pBack) },
			{ "PrincessUpOffset", std::format("{:.1f}", g.pUp) },
			{ "PrincessSideOffset", std::format("{:.1f}", g.pSide) },
			{ "PrincessTurnDegrees", std::format("{:.0f}", g.pTurn) },
		};
		const char*              path = "Data/SKSE/Plugins/RideAlong.ini";
		std::vector<std::string> lines;
		{
			std::ifstream in(path);
			for (std::string line; std::getline(in, line);) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				lines.push_back(line);
			}
		}
		std::map<std::string, bool> written;
		for (auto& line : lines) {
			const auto t = Trim(line);
			if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') continue;
			const auto eq = t.find('=');
			if (eq == std::string::npos) continue;
			const auto key = Trim(t.substr(0, eq));
			if (auto it = values.find(key); it != values.end() && !written[key]) {
				line = key + " = " + it->second;
				written[key] = true;
			}
		}
		bool header = false;
		for (const auto& [key, val] : values) {
			if (written[key]) continue;
			if (!header) {
				lines.push_back("");
				lines.push_back("; ---- saved by the in-game settings page");
				header = true;
			}
			lines.push_back(key + " = " + val);
		}
		std::ofstream out(path, std::ios::trunc);
		for (const auto& line : lines) out << line << "\n";
		logger::info("settings saved to RideAlong.ini");
	}

	// ------------------------------------------------------------------ RideAlong.esp (optional): dialogue, passenger faction
	constexpr const char* PLUGIN_FILE = "RideAlong.esp";
	RE::TESFaction*    passengerFaction{ nullptr };  // OAR can test it: IsInFaction RideAlong.esp|0x802
	RE::TESGlobal*     dialogueGlobal{ nullptr };
	RE::TESTopic*      rideTopic{ nullptr };
	RE::TESTopic*      offTopic{ nullptr };
	RE::TESQuest*      dialogueQuest{ nullptr };

	void LoadForms()
	{
		auto dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(PLUGIN_FILE)) {
			logger::info("{} is not active - dialogue and the passenger faction are off (the hotkey still works)", PLUGIN_FILE);
			return;
		}
		dialogueQuest = dh->LookupForm<RE::TESQuest>(0x800, PLUGIN_FILE);
		passengerFaction = dh->LookupForm<RE::TESFaction>(0x802, PLUGIN_FILE);
		dialogueGlobal = dh->LookupForm<RE::TESGlobal>(0x803, PLUGIN_FILE);
		rideTopic = dh->LookupForm<RE::TESTopic>(0x810, PLUGIN_FILE);
		offTopic = dh->LookupForm<RE::TESTopic>(0x811, PLUGIN_FILE);
		logger::info("{}: quest {} faction {} global {} topics {} {}", PLUGIN_FILE, dialogueQuest != nullptr, passengerFaction != nullptr,
			dialogueGlobal != nullptr, rideTopic != nullptr, offTopic != nullptr);
	}

	void ApplyDialogueSetting(bool a_startQuest)
	{
		if (dialogueGlobal) {
			dialogueGlobal->value = g.dialogueEnabled ? 1.0f : 0.0f;
		}
		if (a_startQuest && dialogueQuest && !dialogueQuest->IsRunning()) {
			dialogueQuest->Start();  // a save made before the plugin was installed
		}
	}

	void SetPassengerTag(RE::Actor* a_actor, bool a_on)
	{
		if (!passengerFaction || !a_actor) return;
		if (a_on) {
			a_actor->AddToFaction(passengerFaction, 0);
		} else {
			a_actor->RemoveFromFaction(passengerFaction);
		}
	}

	bool IsFemale(RE::Actor* a_actor)
	{
		auto base = a_actor ? a_actor->GetActorBase() : nullptr;
		return base && base->GetSex() == RE::SEX::kFemale;
	}

	// ------------------------------------------------------------------ state
	RE::ActorHandle passengerH;
	RE::ActorHandle horseH;
	bool            posePending{ false };
	bool            leaving{ false };
	std::uint32_t   frames{ 0 };
	std::uint32_t   cellMoveTime{ 0 };  // ms timestamp of the last cross-cell move (lets it settle before checking again)
	std::uint32_t   boardTime{ 0 };
	bool            dumpedMoving{ false };
	std::uint32_t   poseCheckTime{ 0 };
	std::uint32_t   poseResends{ 0 };
	RE::NiPoint3    seatPos{};          // where the passenger sat last frame (the movement hook keeps her there)
	float           seatYaw{ 0.0f };
	std::atomic<bool> haveSeat{ false };
	std::uint32_t   combatCheckTime{ 0 };
	std::uint32_t   combatStops{ 0 };   // how often she was pulled out of a fight this ride (for the log)
	// 0.2
	bool            princessNow{ false };   // the current passenger sits sideways
	bool            dismounting{ false };   // playing the dismount animation, still pinned to the seat
	bool            dismountAnnounce{ false };
	std::uint32_t   dismountStart{ 0 };
	RE::ActorHandle pendingH;               // agreed to ride (in dialogue) while you were on foot
	bool            pendingQueued{ false };
	RE::ActorHandle askedRideH;             // chosen in dialogue, handled when the dialogue closes
	RE::ActorHandle askedOffH;
	bool            padFired{ false };

	// ------------------------------------------------------------------ behavior graph variables
	enum class VarType
	{
		kBool,
		kInt,
		kFloat,
		kOther
	};

	struct GraphVar
	{
		RE::BSFixedString name;
		VarType           type;
	};

	// every variable in an actor's main behavior graph, with its type
	std::vector<GraphVar> ListGraphVars(RE::Actor* a_actor)
	{
		std::vector<GraphVar>                          out;
		RE::BSTSmartPointer<RE::BSAnimationGraphManager> mgr;
		if (!a_actor->GetAnimationGraphManager(mgr) || !mgr || mgr->graphs.empty() || !mgr->graphs[0]) {
			return out;
		}
		const auto graph = mgr->graphs[0]->behaviorGraph;
		if (!graph || !graph->data.get() || !graph->data->stringData.get()) {
			return out;
		}
		const auto& names = graph->data->stringData->variableNames;
		const auto& infos = graph->data->variableInfos;
		for (std::int32_t i = 0; i < names.size() && i < infos.size(); ++i) {
			const char* n = names[i].c_str();
			if (!n) {
				continue;
			}
			VarType t = VarType::kOther;
			switch (infos[i].m_type.get()) {
			case RE::hkbVariableInfo::VARIABLE_TYPE_BOOL:
				t = VarType::kBool;
				break;
			case RE::hkbVariableInfo::VARIABLE_TYPE_INT8:
			case RE::hkbVariableInfo::VARIABLE_TYPE_INT16:
			case RE::hkbVariableInfo::VARIABLE_TYPE_INT32:
				t = VarType::kInt;
				break;
			case RE::hkbVariableInfo::VARIABLE_TYPE_REAL:
				t = VarType::kFloat;
				break;
			default:
				break;
			}
			out.push_back({ RE::BSFixedString(n), t });
		}
		return out;
	}

	void DumpGraphVars(RE::Actor* a_actor, const char* a_tag)
	{
		const auto vars = ListGraphVars(a_actor);
		std::string line;
		for (const auto& v : vars) {
			std::string val;
			if (v.type == VarType::kBool) {
				bool b = false;
				if (a_actor->GetGraphVariableBool(v.name, b)) val = b ? "1" : "0";
			} else if (v.type == VarType::kInt) {
				std::int32_t x = 0;
				if (a_actor->GetGraphVariableInt(v.name, x)) val = std::to_string(x);
			} else if (v.type == VarType::kFloat) {
				float f = 0.0f;
				if (a_actor->GetGraphVariableFloat(v.name, f)) val = std::format("{:.3f}", f);
			}
			if (val.empty() || val == "0" || val == "0.000") {
				continue;  // only non-zero values, to keep the log readable
			}
			line += std::format("{}={} ", v.name.c_str(), val);
		}
		logger::info("graph vars [{}] {} ({} total, non-zero shown): {}", a_tag, a_actor->GetDisplayFullName(), vars.size(), line);
	}

	// ------------------------------------------------------------------ rider pose
	// The rider's clips are synchronized to the horse they sit on, so on a passenger (who has no horse of its own)
	// they never advance. Instead the passenger's skeleton copies the player's - who is animated by the real horse -
	// every frame, after animation and before the frame is drawn. Head, face and gear bones keep the passenger's own.
	struct BonePair
	{
		RE::NiAVObject* to;
		RE::NiAVObject* from;
		bool            move;
	};
	std::vector<BonePair> bones;
	RE::NiAVObject*       bonesTo{ nullptr };
	RE::NiAVObject*       bonesFrom{ nullptr };

	bool ListHas(const std::string& a_list, std::string_view a_name, bool a_substring)
	{
		std::size_t start = 0;
		while (start <= a_list.size()) {
			auto comma = a_list.find(',', start);
			if (comma == std::string::npos) {
				comma = a_list.size();
			}
			const auto item = Trim(a_list.substr(start, comma - start));
			if (!item.empty() && (a_substring ? a_name.find(item) != std::string_view::npos : a_name == item)) {
				return true;
			}
			start = comma + 1;
		}
		return false;
	}

	void CollectBones(RE::NiAVObject* a_obj, RE::NiAVObject* a_fromRoot)
	{
		if (!a_obj) {
			return;
		}
		const char* n = a_obj->name.c_str();
		if (n && std::string_view(n).starts_with("NPC ") && (g.copyBones.empty() || ListHas(g.copyBones, n, true)) && !ListHas(g.skipBones, n, true)) {
			if (auto from = a_fromRoot->GetObjectByName(a_obj->name)) {
				bones.push_back({ a_obj, from, ListHas(g.moveBones, n, false) });
			}
		}
		if (auto node = a_obj->AsNode()) {
			for (auto& child : node->GetChildren()) {
				if (child) {
					CollectBones(child.get(), a_fromRoot);
				}
			}
		}
	}

	void CopyPose(RE::Actor* a_passenger, RE::Actor* a_rider)
	{
		auto to = a_passenger->Get3D(false);
		auto from = a_rider->Get3D(false);
		if (!to || !from) {
			return;
		}
		if (to != bonesTo || from != bonesFrom) {
			bones.clear();
			CollectBones(to, from);
			bonesTo = to;
			bonesFrom = from;
			logger::info("pose copy: {} bones linked", bones.size());
		}
		for (const auto& b : bones) {
			b.to->local.rotate = b.from->local.rotate;
			if (b.move) {
				b.to->local.translate = b.from->local.translate;
			}
		}
		// refresh only the copied limbs (not the whole body, which upsets cloth and hair physics)
		RE::NiUpdateData ctx{};
		for (const auto& b : bones) {
			b.to->Update(ctx);
		}
	}

	// the rider's animation follows the horse through graph variables the engine writes on the rider each frame;
	// the passenger has no horse of its own, so it gets the player's values
	std::vector<GraphVar> syncList;

	void BuildSyncList(RE::Actor* a_from)
	{
		syncList.clear();
		const auto        all = ListGraphVars(a_from);
		std::string       wanted = g.syncVars + ",";
		std::size_t       start = 0;
		std::string       found, missing;
		while (true) {
			const auto comma = wanted.find(',', start);
			if (comma == std::string::npos) {
				break;
			}
			const auto name = Trim(wanted.substr(start, comma - start));
			start = comma + 1;
			if (name.empty()) {
				continue;
			}
			bool hit = false;
			for (const auto& v : all) {
				if (_stricmp(v.name.c_str(), name.c_str()) == 0 && v.type != VarType::kOther) {
					syncList.push_back(v);
					hit = true;
					break;
				}
			}
			(hit ? found : missing) += name + " ";
		}
		logger::info("sync list: found [{}] not in graph [{}]", found, missing);
	}

	void SyncGraphVars(RE::Actor* a_from, RE::Actor* a_to)
	{
		for (const auto& v : syncList) {
			switch (v.type) {
			case VarType::kBool:
				{
					bool b = false;
					if (a_from->GetGraphVariableBool(v.name, b)) a_to->SetGraphVariableBool(v.name, b);
				}
				break;
			case VarType::kInt:
				{
					std::int32_t x = 0;
					if (a_from->GetGraphVariableInt(v.name, x)) a_to->SetGraphVariableInt(v.name, x);
				}
				break;
			case VarType::kFloat:
				{
					float f = 0.0f;
					if (a_from->GetGraphVariableFloat(v.name, f)) a_to->SetGraphVariableFloat(v.name, f);
				}
				break;
			default:
				break;
			}
		}
	}

	void Notify(const std::string& a_text)
	{
		RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
		logger::info("{}", a_text);
	}

	void Reset()
	{
		haveSeat = false;
		passengerH.reset();
		horseH.reset();
		posePending = false;
		leaving = false;
		cellMoveTime = 0;
		poseResends = 0;
		bones.clear();
		bonesTo = nullptr;
		bonesFrom = nullptr;
		princessNow = false;
		dismounting = false;
	}

	// Papyrus MoveTo: moves a reference and rebuilds its physics (gives the passenger its character controller back)
	void MoveTo(RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_target, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_rot)
	{
		using func_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
		static REL::Relocation<func_t> func{ RELOCATION_ID(56227, 56626) };
		const RE::ObjectRefHandle      handle = a_target->RE::TESObjectREFR::GetHandle();
		func(a_ref, handle, a_target->GetParentCell(), a_target->GetWorldspace(), a_pos, a_rot);
	}

	// Keeps the passenger out of fights while she rides: her AI stays on (so you can talk to her and she follows
	// her packages as usual), but any combat she's drawn into is ended and her weapon goes back away at once.
	// Calm: she won't start fights or join yours (Aggression 0 = unaggressive, Assistance 0 = helps nobody).
	// Restored from her own NPC record when she gets off.
	void SetCalm(RE::Actor* a_actor, bool a_calm)
	{
		auto avo = a_actor ? a_actor->AsActorValueOwner() : nullptr;
		auto npc = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!avo || !npc) return;
		auto base = static_cast<RE::ActorValueOwner*>(npc);
		for (auto av : { RE::ActorValue::kAggression, RE::ActorValue::kAssistance }) {
			avo->SetActorValue(av, a_calm ? 0.0f : base->GetActorValue(av));
		}
	}

	// Keeps her out of anything that still reaches her (an enemy attacking her directly). Her weapon is NOT forced
	// away every frame - that fought her AI and made her draw and sheathe over and over. It goes away once the
	// fight is over.
	void KeepPeaceful(RE::Actor* a_passenger)
	{
		if (a_passenger->IsInCombat()) {
			a_passenger->StopCombat();
			if (combatStops++ % 200 == 0) {
				logger::info("{}: kept out of the fight ({}x this ride)", a_passenger->GetDisplayFullName(), combatStops);
			}
			return;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (auto st = a_passenger->AsActorState(); st && st->IsWeaponDrawn() && !player->IsInCombat()) {
			a_passenger->DrawWeaponMagicHands(false);
		}
	}

	// After loading nobody is riding: anyone a save caught on the seat loses the passenger tag (and gets back the AI
	// an earlier test build switched off while riding).
	void ReleaseSavedPassengers()
	{
		if (!passengerFaction) return;
		for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
			auto a = h.get();
			if (a && a->IsInFaction(passengerFaction)) {
				SetPassengerTag(a.get(), false);
				SetCalm(a.get(), false);
				a->EnableAI(true);
				a->EvaluatePackage(true, false);
				logger::info("{}: released (saved while riding)", a->GetDisplayFullName());
			}
		}
	}

	// ------------------------------------------------------------------ riding
	bool Place(RE::Actor* a_passenger, RE::Actor* a_horse)
	{
		auto root = a_horse->Get3D();
		if (!root || !a_passenger->Get3D()) {
			return false;
		}
		static RE::BSFixedString anchorName(g.anchorNode.c_str());
		RE::NiAVObject*          anchor = root->GetObjectByName(anchorName);

		const RE::NiPoint3 base = anchor ? anchor->world.translate : root->world.translate;
		const RE::NiPoint3 offset = princessNow ? RE::NiPoint3{ g.pSide, -g.pBack, g.pUp } : RE::NiPoint3{ g.side, -g.back, g.up };
		const RE::NiPoint3 pos = base + root->world.rotate * offset;
		const float        yaw = a_horse->data.angle.z + (princessNow ? g.pTurn * 0.01745329f : 0.0f);

		if (a_passenger->GetCharController()) {
			a_passenger->DetachCharController();  // no capsule: nothing to collide with the horse, no AI walking
		}
		a_passenger->data.location = pos;
		seatPos = pos;
		seatYaw = yaw;
		haveSeat = true;
		a_passenger->data.angle = RE::NiPoint3{ 0.0f, 0.0f, yaw };
		a_passenger->Update3DPosition(true);

		if (frames++ % 600 == 0) {
			logger::info("riding: anchor={} pos=({:.0f},{:.0f},{:.0f})", anchor ? "found" : "MISSING (using horse root)", pos.x, pos.y, pos.z);
		}
		return true;
	}

	// The side-saddle animations are yukl's (YY Anim Replacer - Princess' Horse Riding) and are not shipped with
	// Ride Along: the player copies them from that mod into the Princess Seat add-on's OAR folder. Without them she
	// rides astride as normal.
	bool PrincessAnimsInstalled()
	{
		static int state = -1;
		if (state < 0) {
			std::error_code ec;
			state = std::filesystem::exists("Data/meshes/actors/character/animations/OpenAnimationReplacer/RideAlongPrincess/Passenger/horse_rider/idle.hkx", ec) ? 1 : 0;
			logger::info("princess seat animations: {}", state ? "found" : "NOT found - copy YY Anim Replacer - Princess' Horse Riding's horse_rider folder into the Princess Seat add-on (see its readme)");
		}
		return state == 1;
	}

	void Board(RE::Actor* a_passenger, RE::Actor* a_horse)
	{
		passengerH = a_passenger->GetHandle();
		horseH = a_horse->GetHandle();
		leaving = false;
		dismounting = false;
		frames = 0;
		pendingH.reset();
		princessNow = g.princess && IsFemale(a_passenger) && PrincessAnimsInstalled();
		SetPassengerTag(a_passenger, true);  // before the pose event, so an OAR replacer sees it on the first clip
		if (g.noCombat) {
			a_passenger->StopCombat();
			a_passenger->DrawWeaponMagicHands(false);  // weapon away for the ride
			SetCalm(a_passenger, true);
		}
		combatStops = 0;
		a_passenger->DetachCharController();
		Place(a_passenger, a_horse);
		const bool ok = a_passenger->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
		logger::info("board: {} -> pose event '{}' {}", a_passenger->GetDisplayFullName(), g.poseEvent, ok ? "accepted" : "REJECTED");

		auto player = RE::PlayerCharacter::GetSingleton();
		BuildSyncList(player);
		boardTime = RE::GetDurationOfApplicationRunTime();
		dumpedMoving = false;
		if (g.dumpGraphVars) {
			DumpGraphVars(player, "player, on board");
			DumpGraphVars(a_passenger, "passenger, on board");
		}
		Notify(std::format("{} rides with you.", a_passenger->GetDisplayFullName()));
	}

	void FinishAlight(bool a_announce)
	{
		haveSeat = false;
		auto passenger = passengerH.get();
		auto horse = horseH.get();
		Reset();
		if (!passenger) {
			return;
		}
		// Leave the passenger tag on for a moment: taking it off swaps the OAR animation set (princess seat) while
		// the graph is still leaving the riding state, which can crash the game. Drop it once she's on her feet.
		{
			RE::ActorHandle h = passenger->GetHandle();
			std::thread([h] {
				std::this_thread::sleep_for(std::chrono::milliseconds(2000));
				SKSE::GetTaskInterface()->AddTask([h] {
					auto a = h.get();
					if (a && a != passengerH.get()) SetPassengerTag(a.get(), false);  // unless she climbed back on
				});
			}).detach();
		}
		// Step 1 now: end the riding animation. Steps 2 and 3 a few frames later - doing the animation change
		// and the move (which rebuilds her physics) in the same frame crashed the game in the cloth/hair physics (FSMP).
		SetCalm(passenger.get(), false);
		passenger->NotifyAnimationGraph(RE::BSFixedString(g.exitEvent.c_str()));
		logger::info("alight: {}", passenger->GetDisplayFullName());
		if (a_announce) {
			Notify(std::format("{} gets off.", passenger->GetDisplayFullName()));
		}
		const RE::ActorHandle ph = passenger->GetHandle();
		const RE::ActorHandle hh = horse ? horse->GetHandle() : RE::ActorHandle{};
		std::thread([ph, hh] {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			SKSE::GetTaskInterface()->AddTask([ph, hh] {
				auto p = ph.get();
				if (!p || p == passengerH.get() || p->IsDead() || !p->Is3DLoaded()) {
					return;  // gone, dead, unloaded, or already back on the horse
				}
				auto               h = hh.get();
				RE::Actor*         anchor = (h && !h->IsDead()) ? h.get() : RE::PlayerCharacter::GetSingleton();
				const float        a = anchor->data.angle.z;
				const RE::NiPoint3 spot = anchor->data.location + RE::NiPoint3{ -std::cos(a) * 110.0f, std::sin(a) * 110.0f, 0.0f };  // the horse's left
				MoveTo(p.get(), anchor, spot, RE::NiPoint3{ 0.0f, 0.0f, a });
				logger::info("alight: set down beside the horse");
				std::thread([ph] {
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
					SKSE::GetTaskInterface()->AddTask([ph] {
						auto p = ph.get();
						if (!p || p == passengerH.get() || p->IsDead()) {
							return;
						}
						p->EvaluatePackage(true, false);
					});
				}).detach();
			});
		}).detach();
	}

	// Getting off: the dismount animation plays on the seat first (she stays pinned to the horse while it does),
	// then she is set down beside the horse.
	void Alight(bool a_announce)
	{
		if (dismounting) {
			return;
		}
		auto passenger = passengerH.get();
		auto horse = horseH.get();
		// the side-saddle seat has no dismount clip of its own: the normal one (made for straddling the horse)
		// throws her up over the rider, so without a princess dismount event she just steps down
		const std::string& ev = princessNow ? g.pDismountEvent : g.dismountEvent;
		if (g.dismountAnim && passenger && horse && !horse->IsDead() && !passenger->IsDead() && !ev.empty() &&
			passenger->NotifyAnimationGraph(RE::BSFixedString(ev.c_str()))) {
			dismounting = true;
			dismountAnnounce = a_announce;
			dismountStart = RE::GetDurationOfApplicationRunTime();
			leaving = false;
			logger::info("dismount: '{}' playing for {:.1f}s", ev, g.dismountSeconds);
			return;
		}
		FinishAlight(a_announce);
	}

	bool IsHumanoid(RE::Actor* a_actor)
	{
		const auto race = a_actor->GetRace();
		return race && race->HasKeywordString("ActorTypeNPC"sv);
	}

	RE::NiPointer<RE::Actor> FindFollower(RE::PlayerCharacter* a_player, RE::Actor* a_horse)
	{
		RE::NiPointer<RE::Actor> best;
		float                    bestDist = g.range;
		for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
			auto ptr = h.get();
			auto a = ptr.get();
			if (!a || a == a_player || a == a_horse || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() ||
				!a->IsPlayerTeammate() || a->IsOnMount() || a->IsAMount() || !IsHumanoid(a)) {
				continue;
			}
			const float d = a->GetDistance(a_player);
			if (d < bestDist) {
				bestDist = d;
				best = ptr;
			}
		}
		return best;
	}

	void Toggle()
	{
		if (dismounting) {
			return;
		}
		if (passengerH) {
			Alight(true);
			return;
		}
		auto                     player = RE::PlayerCharacter::GetSingleton();
		RE::NiPointer<RE::Actor> horse;
		if (!player->IsOnMount() || !player->GetMount(horse) || !horse) {
			Notify("Mount your horse first.");
			return;
		}
		auto follower = FindFollower(player, horse.get());
		if (!follower) {
			Notify("No follower is close enough to ride with you.");
			return;
		}
		Board(follower.get(), horse.get());
	}

	// ------------------------------------------------------------------ dialogue: "Ride with me." / "You can get down now."
	void BoardPending()
	{
		pendingQueued = false;
		auto follower = pendingH.get();
		pendingH.reset();
		auto                     player = RE::PlayerCharacter::GetSingleton();
		RE::NiPointer<RE::Actor> horse;
		if (!follower || passengerH || !player->IsOnMount() || !player->GetMount(horse) || !horse) {
			return;
		}
		if (follower->IsDead() || follower->IsDisabled() || !follower->IsPlayerTeammate() || follower->IsOnMount() || !IsHumanoid(follower.get())) {
			return;  // dismissed, on a horse of her own, or otherwise not coming
		}
		if (!follower->Is3DLoaded() || follower->GetDistance(player) > (g.range > 3000.0f ? g.range : 3000.0f)) {
			Notify(std::format("{} is too far away to climb on.", follower->GetDisplayFullName()));
			return;
		}
		Board(follower.get(), horse.get());
	}

	// Writes to the log why "Ride with me." / "You can get down now." do or don't show for this NPC.
	void LogDialogueCheck()
	{
		auto mtm = RE::MenuTopicManager::GetSingleton();
		RE::NiPointer<RE::TESObjectREFR> ref;
		if (mtm) ref = mtm->speaker.get();
		auto npc = ref ? ref->As<RE::Actor>() : nullptr;
		if (!npc) return;
		auto player = RE::PlayerCharacter::GetSingleton();
		std::string line = std::format("dialogue with {}: quest running={} global={} teammate={} passenger={} playerMounted={} |",
			npc->GetDisplayFullName(), dialogueQuest && dialogueQuest->IsRunning(), dialogueGlobal ? dialogueGlobal->value : -1.0f,
			npc->IsPlayerTeammate(), passengerFaction && npc->IsInFaction(passengerFaction), player->IsOnMount());
		for (auto [topic, name] : { std::pair{ rideTopic, "Ride" }, std::pair{ offTopic, "Off" } }) {
			if (!topic) { line += std::format(" {}: no topic", name); continue; }
			int pass = 0;
			for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
				auto info = topic->topicInfos[i];
				if (info && info->objConditions.IsTrue(npc, player)) ++pass;
			}
			line += std::format(" {}: {} of {} lines pass", name, pass, topic->numTopicInfos);
		}
		logger::info("{}", line);
	}

	void HandleDialogueChoice()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (auto off = askedOffH.get()) {
			askedOffH.reset();
			if (pendingH.get().get() == off.get()) {
				pendingH.reset();
			}
			if (passengerH.get().get() == off.get()) {
				Alight(true);
			}
		}
		if (auto rider = askedRideH.get()) {
			askedRideH.reset();
			if (passengerH) {
				Notify(std::format("{} is already riding with you.", passengerH.get() ? passengerH.get()->GetDisplayFullName() : "Someone"));
				return;
			}
			RE::NiPointer<RE::Actor> horse;
			if (player->IsOnMount() && player->GetMount(horse) && horse) {
				Board(rider.get(), horse.get());
			} else {
				pendingH = rider->GetHandle();
				Notify(std::format("{} will climb on behind you when you mount.", rider->GetDisplayFullName()));
			}
		}
	}

	// runs every frame from the camera update: after the horse has moved, before the frame is drawn
	void Tick()
	{
		if (dismounting) {
			auto passenger = passengerH.get();
			auto horse = horseH.get();
			const auto now = RE::GetDurationOfApplicationRunTime();
			if (!passenger || !horse || horse->IsDead() || passenger->IsDead() || now - dismountStart >= static_cast<std::uint32_t>(g.dismountSeconds * 1000.0f)) {
				if (!leaving) {
					leaving = true;
					const bool announce = dismountAnnounce;
					SKSE::GetTaskInterface()->AddTask([announce] { FinishAlight(announce); });
				}
				return;
			}
			Place(passenger.get(), horse.get());  // keep her on the seat while the animation plays
			return;
		}
		if (!passengerH && pendingH && !pendingQueued) {
			auto player = RE::PlayerCharacter::GetSingleton();
			if (player && player->IsOnMount()) {
				pendingQueued = true;
				SKSE::GetTaskInterface()->AddTask([] { BoardPending(); });
			}
		}
		if (!passengerH || leaving) {
			return;
		}
		auto passenger = passengerH.get();
		auto horse = horseH.get();
		if (!passenger || passenger->IsDead() || !horse || horse->IsDead()) {
			leaving = true;
			SKSE::GetTaskInterface()->AddTask([] { Alight(false); });
			return;
		}
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player->IsOnMount()) {
			leaving = true;
			SKSE::GetTaskInterface()->AddTask([] { Alight(true); });
			return;
		}
		const auto now = RE::GetDurationOfApplicationRunTime();
		// Outdoors she is only moved along when her cell is about to unload or she is in another worldspace; a plain
		// cell border is fine (the move rebuilds her, which is what knocked her out of the pose for a moment).
		const auto hc = horse->GetParentCell();
		const auto pc = passenger->GetParentCell();
		const bool needMove = hc && pc != hc &&
		                      (!pc || hc->IsInteriorCell() || pc->IsInteriorCell() || !pc->IsAttached() ||
		                          passenger->GetWorldspace() != horse->GetWorldspace() || !passenger->Is3DLoaded());
		if (needMove && now - cellMoveTime > 2000) {
			cellMoveTime = now;
			// the horse crossed into another cell (or we fast-travelled): bring the passenger along, pose again
			leaving = true;
			SKSE::GetTaskInterface()->AddTask([] {
				auto p = passengerH.get();
				auto h = horseH.get();
				leaving = false;
				if (p && h) {
					haveSeat = false;  // the old seat is in the old cell: let the move stand until she is placed again
					MoveTo(p.get(), h.get(), h->data.location, h->data.angle);
					posePending = true;
					bones.clear();  // her 3D may be rebuilt: link the bones again
					bonesTo = nullptr;
					logger::info("cell change: passenger moved along");
				}
			});
			return;
		}
		if (!Place(passenger.get(), horse.get())) {
			return;
		}
		if (posePending) {
			posePending = false;
			passenger->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
		}
		// pose watchdog: a cell change (MoveTo rebuilds the actor) or her AI can knock her out of the riding
		// pose into a standing/falling one - then she hovers a body's height above the saddle. Put her back.
		if (now - poseCheckTime >= 0) {  // every frame: put her back before the next frame is drawn
			poseCheckTime = now;
			bool riding = true;
			if (passenger->GetGraphVariableBool(RE::BSFixedString("bIsRiding"), riding) && !riding) {
				const bool ok = passenger->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
				if (poseResends++ < 20) {
					std::int32_t st = -1;
					passenger->GetGraphVariableInt(RE::BSFixedString("iState"), st);
					logger::info("pose lost (iState={}) - resent '{}' {}", st, g.poseEvent, ok ? "accepted" : "REJECTED");
				}
			}
		}
		if (g.noCombat) {
			KeepPeaceful(passenger.get());  // every frame, so a fight never gets as far as pulling her out of the seat
		}
		SyncGraphVars(player, passenger.get());
		if (princessNow ? g.princessCopyPose : g.copyPose) {
			CopyPose(passenger.get(), player);
		}
		if (g.dumpGraphVars && !dumpedMoving && now - boardTime > 8000) {
			float speed = 0.0f;
			horse->GetGraphVariableFloat(RE::BSFixedString("Speed"), speed);
			if (speed > 150.0f) {  // riding at a decent pace: record what the rider's graph looks like in motion
				dumpedMoving = true;
				DumpGraphVars(player, std::format("player, moving, horse Speed={:.0f}", speed).c_str());
				DumpGraphVars(passenger.get(), "passenger, moving");
			}
		}
	}

	// ------------------------------------------------------------------ hooks
	using CamUpdate_t = void(RE::TESCameraState*, RE::BSTSmartPointer<RE::TESCameraState>&);

	template <std::size_t I>
	struct CameraHook
	{
		static void Thunk(RE::TESCameraState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_next)
		{
			original(a_this, a_next);
			Tick();
		}

		static void Install(REL::VariantID a_vtable, const char* a_name)
		{
			REL::Relocation<std::uintptr_t> vtbl{ a_vtable };
			original = reinterpret_cast<CamUpdate_t*>(vtbl.write_vfunc(3, reinterpret_cast<std::uintptr_t>(&Thunk)));
			logger::info("hooked {}::Update", a_name);
		}

		static inline CamUpdate_t* original{ nullptr };
	};

	// The passenger's own movement step (Actor::ApplyMovementDelta, called from the actor update): while she rides,
	// her AI / animation movement is not applied at all - she stays exactly on the seat instead of being walked off
	// it and snapped back each frame (the "floating" and pose resets). Same approach as RSE - Shoulder or Saddle.
	struct MovementHook
	{
		static void Thunk(RE::Actor* a_actor, float a_delta)
		{
			if (a_actor && haveSeat && !leaving && passengerH) {
				auto p = passengerH.get();
				if (p && p.get() == a_actor) {
					a_actor->data.location = seatPos;
					a_actor->data.angle.z = seatYaw;
					return;  // skip the engine's movement for the passenger
				}
			}
			original(a_actor, a_delta);
		}

		static void Install()
		{
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(36359, 37350), REL::VariantOffset(0xF0, 0xFB, 0xF0) };
			auto& trampoline = SKSE::GetTrampoline();
			original = trampoline.write_call<5>(target.address(), Thunk);
			logger::info("hooked Actor::ApplyMovementDelta");
		}

		static inline REL::Relocation<decltype(Thunk)> original;
	};

	class InputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputSink* Get()
		{
			static InputSink sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto ui = RE::UI::GetSingleton();
			if (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			for (auto e = *a_event; e; e = e->next) {
				if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
					continue;
				}
				const auto button = e->AsButtonEvent();
				if (!button) {
					continue;
				}
				if (e->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
					if (g.hotkeyEnabled && button->IsDown() && button->GetIDCode() == g.hotkey) {
						SKSE::GetTaskInterface()->AddTask([] { Toggle(); });
					}
				} else if (e->GetDevice() == RE::INPUT_DEVICE::kGamepad && g.padButton != 0 && button->GetIDCode() == g.padButton) {
					// held, not tapped: every controller button already does something while riding
					if (button->IsUp()) {
						padFired = false;
					} else if (button->IsPressed() && !padFired && button->HeldDuration() >= g.padHold) {
						padFired = true;
						SKSE::GetTaskInterface()->AddTask([] { Toggle(); });
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

namespace RideAlong
{
	// the dialogue choice is acted on once the conversation closes, so the boarding isn't hidden behind the dialogue camera
	class EventSink :
		public RE::BSTEventSink<RE::TESTopicInfoEvent>,
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static EventSink* Get()
		{
			static EventSink sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* a_event, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override
		{
			if (!a_event || !a_event->speakerRef || a_event->type.get() != RE::TESTopicInfoEvent::TopicInfoEventType::kTopicBegin ||
				(!rideTopic && !offTopic)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto info = RE::TESForm::LookupByID<RE::TESTopicInfo>(a_event->topicInfoFormID);
			auto actor = a_event->speakerRef->As<RE::Actor>();
			if (!info || !actor) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (info->parentTopic && info->parentTopic == rideTopic) {
				askedRideH = actor->GetHandle();
			} else if (info->parentTopic && info->parentTopic == offTopic) {
				askedOffH = actor->GetHandle();
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->opening && std::string_view(a_event->menuName.c_str()) == RE::DialogueMenu::MENU_NAME) {
				SKSE::GetTaskInterface()->AddTask([] { LogDialogueCheck(); });
			}
			if (a_event && !a_event->opening && std::string_view(a_event->menuName.c_str()) == RE::DialogueMenu::MENU_NAME && (askedRideH || askedOffH)) {
				SKSE::GetTaskInterface()->AddTask([] { HandleDialogueChoice(); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// ------------------------------------------------------------------ SKSE Menu Framework page (optional)
	namespace Menu
	{
		struct Choice
		{
			const char*   name;
			std::uint32_t code;
		};
		// DirectX scan codes
		constexpr Choice KEYS[] = { { "K", 0x25 }, { "G", 0x22 }, { "V", 0x2F }, { "B", 0x30 }, { "H", 0x23 }, { "J", 0x24 }, { "L", 0x26 },
			{ "N", 0x31 }, { "M", 0x32 }, { "O", 0x18 }, { "P", 0x19 }, { "U", 0x16 }, { "Y", 0x15 }, { "T", 0x14 }, { "X", 0x2D },
			{ "C", 0x2E }, { "Z", 0x2C }, { "F1", 0x3B }, { "F2", 0x3C }, { "F3", 0x3D }, { "F4", 0x3E }, { "F5", 0x3F }, { "F6", 0x40 },
			{ "F7", 0x41 }, { "F8", 0x42 }, { "F9", 0x43 }, { "F10", 0x44 }, { "F11", 0x57 }, { "F12", 0x58 }, { "Numpad 0", 0x52 },
			{ "Numpad 1", 0x4F }, { "Numpad 2", 0x50 }, { "Numpad 3", 0x51 }, { "Numpad 4", 0x4B }, { "Numpad 5", 0x4C },
			{ "Numpad 6", 0x4D }, { "Numpad 7", 0x47 }, { "Numpad 8", 0x48 }, { "Numpad 9", 0x49 }, { "Insert", 0xD2 }, { "Home", 0xC7 },
			{ "Page Up", 0xC9 }, { "Delete", 0xD3 }, { "End", 0xCF }, { "Page Down", 0xD1 }, { "Left Alt", 0x38 }, { "Right Alt", 0xB8 },
			{ "Right Ctrl", 0x9D }, { "Caps Lock", 0x3A }, { "Backspace", 0x0E }, { "` (tilde)", 0x29 } };
		// controller buttons as SKSE reports them
		constexpr Choice PAD[] = { { "None", 0x0 }, { "D-pad Up", 0x0001 }, { "D-pad Down", 0x0002 }, { "D-pad Left", 0x0004 },
			{ "D-pad Right", 0x0008 }, { "Start", 0x0010 }, { "Back / View", 0x0020 }, { "Left stick (click)", 0x0040 },
			{ "Right stick (click)", 0x0080 }, { "LB", 0x0100 }, { "RB", 0x0200 }, { "A", 0x1000 }, { "B", 0x2000 }, { "X", 0x4000 },
			{ "Y", 0x8000 }, { "LT", 0x0009 }, { "RT", 0x000A } };

		template <std::size_t N>
		bool PickCombo(const char* a_label, const Choice (&a_list)[N], std::uint32_t& a_code)
		{
			const char* names[N];
			int         current = -1;
			for (std::size_t i = 0; i < N; ++i) {
				names[i] = a_list[i].name;
				if (a_list[i].code == a_code) current = static_cast<int>(i);
			}
			if (current < 0) {
				ImGui::Text("%s: custom code 0x%X (from RideAlong.ini)", a_label, a_code);
				current = 0;
				if (!ImGui::Button("Use the list instead")) return false;
				a_code = a_list[0].code;
				return true;
			}
			if (ImGui::Combo(a_label, &current, names, static_cast<int>(N))) {
				a_code = a_list[current].code;
				return true;
			}
			return false;
		}

		bool        dirty = false;
		std::string status;

		void __stdcall Render()
		{
			ImGui::TextWrapped("%s", "Changes apply right away. Press Save to keep them for next time.");

			ImGui::SeparatorText("Getting on and off");
			dirty |= ImGui::Checkbox("Use the keyboard key", &g.hotkeyEnabled);
			dirty |= PickCombo("Keyboard key", KEYS, g.hotkey);
			dirty |= PickCombo("Controller button (hold)", PAD, g.padButton);
			dirty |= ImGui::SliderFloat("Hold time (seconds)", &g.padHold, 0.0f, 2.0f, "%.1f");
			if (ImGui::Checkbox("\"Ride with me.\" in follower dialogue", &g.dialogueEnabled)) {
				dirty = true;
				SKSE::GetTaskInterface()->AddTask([] { ApplyDialogueSetting(false); });  // game data: on the main thread
			}
			if (!passengerFaction) {
				ImGui::TextWrapped("%s", "(RideAlong.esp isn't active: the dialogue option needs it.)");
			}
			dirty |= ImGui::SliderFloat("Follower range", &g.range, 300.0f, 4000.0f, "%.0f");
			dirty |= ImGui::Checkbox("Passenger stays out of fights", &g.noCombat);

			ImGui::SeparatorText("Seat");
			dirty |= ImGui::SliderFloat("Behind the saddle", &g.back, -20.0f, 80.0f, "%.0f");
			dirty |= ImGui::SliderFloat("Height", &g.up, -40.0f, 40.0f, "%.0f");
			dirty |= ImGui::SliderFloat("Left / right", &g.side, -40.0f, 40.0f, "%.0f");

			ImGui::SeparatorText("Getting off");
			dirty |= ImGui::Checkbox("Dismount animation", &g.dismountAnim);
			dirty |= ImGui::SliderFloat("Animation time (seconds)", &g.dismountSeconds, 0.3f, 3.0f, "%.1f");

			ImGui::SeparatorText("Princess seat (female passengers)");
			ImGui::TextWrapped("%s", "Female passengers sit sideways. Needs a sitting animation set up in OAR - see the Ride Along readme.");
			dirty |= ImGui::Checkbox("Use the princess seat", &g.princess);
			if (g.princess && !PrincessAnimsInstalled()) {
				ImGui::TextWrapped("%s", "The side-saddle animations aren't installed: copy the horse_rider folder from YY Anim Replacer - Princess' Horse Riding into the Princess Seat add-on's Passenger folder (see its readme). Until then she rides astride.");
			}
			dirty |= ImGui::SliderFloat("Princess: behind the saddle", &g.pBack, -20.0f, 80.0f, "%.0f");
			dirty |= ImGui::SliderFloat("Princess: height", &g.pUp, -40.0f, 40.0f, "%.0f");
			dirty |= ImGui::SliderFloat("Princess: left / right", &g.pSide, -40.0f, 40.0f, "%.0f");
			dirty |= ImGui::SliderFloat("Princess: turn (degrees)", &g.pTurn, -180.0f, 180.0f, "%.0f");
			dirty |= ImGui::Checkbox("Princess: copy your leg movement", &g.princessCopyPose);
			if (passengerH && passengerH.get()) {
				ImGui::Text("Riding with you: %s%s", passengerH.get()->GetDisplayFullName(), princessNow ? " (princess seat)" : "");
			}

			ImGui::Separator();
			if (ImGui::Button("Save")) {
				SaveSettings();
				dirty = false;
				status = "Saved to RideAlong.ini.";
			}
			if (dirty) {
				ImGui::SameLine();
				ImGui::Text("%s", "Unsaved changes");
			} else if (!status.empty()) {
				ImGui::SameLine();
				ImGui::Text("%s", status.c_str());
			}
		}

		void Register()
		{
			if (!SKSEMenuFramework::IsInstalled()) {
				logger::info("SKSE Menu Framework not installed - settings stay in RideAlong.ini");
				return;
			}
			SKSEMenuFramework::SetSection("Ride Along");
			SKSEMenuFramework::AddSectionItem("Settings", Render);
			logger::info("settings page added to SKSE Menu Framework");
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	logger::info("Ride Along {} loading", SKSE::GetPluginVersion().string());

	RideAlong::LoadSettings();
	SKSE::AllocTrampoline(64);
	RideAlong::MovementHook::Install();
	RideAlong::CameraHook<0>::Install(RE::VTABLE_HorseCameraState[0], "HorseCameraState");
	RideAlong::CameraHook<1>::Install(RE::VTABLE_FirstPersonState[0], "FirstPersonState");
	RideAlong::CameraHook<2>::Install(RE::VTABLE_ThirdPersonState[0], "ThirdPersonState");

	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			RideAlong::Menu::Register();  // every plugin is loaded by now, the menu framework included
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(RideAlong::InputSink::Get());
			RideAlong::LoadForms();
			RideAlong::ApplyDialogueSetting(false);
			if (auto src = RE::ScriptEventSourceHolder::GetSingleton()) {
				src->AddEventSink<RE::TESTopicInfoEvent>(RideAlong::EventSink::Get());
			}
			if (auto ui = RE::UI::GetSingleton()) {
				ui->AddEventSink<RE::MenuOpenCloseEvent>(RideAlong::EventSink::Get());
			}
			logger::info("input listener registered");
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			RideAlong::Reset();
			RideAlong::pendingH.reset();
			RideAlong::askedRideH.reset();
			RideAlong::askedOffH.reset();
			if (a_msg->type == SKSE::MessagingInterface::kNewGame) {
				RideAlong::ApplyDialogueSetting(true);
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			RideAlong::ApplyDialogueSetting(true);
			RideAlong::ReleaseSavedPassengers();
			break;
		default:
			break;
		}
	});
	return true;
}
