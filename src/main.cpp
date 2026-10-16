// Ride Along - SKSE plugin: your followers ride with you on your horse.
// Copyright (C) 2026 Clyde
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License (LICENSE)
// for more details. Built on CommonLibSSE-NG (GPL-3.0-or-later with exceptions).

// Ride Along - up to four followers ride with you on your horse: two behind you, one in front, one on your shoulders.
// Each passenger is pinned to their seat every frame, right after the game has moved the horse and before the frame
// is drawn (in the camera update), so they cannot fall behind. Their character controller is detached while they
// ride, so they have no physics to fight the horse with.
//
// One DLL for Skyrim SE 1.5.97, AE 1.6.x and VR (CommonLibSSE-NG, Address Library). Version-specific hooks are checked
// before they are written and skipped (with a log line) if the game code doesn't look as expected.

#include <algorithm>
#include <array>
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
	// ------------------------------------------------------------------ seats
	enum Seat : int
	{
		kBack = 0,    // behind you (the original seat)
		kBack2,       // behind the first passenger
		kFront,       // in front of you
		kShoulders,   // on your shoulders
		kSeatCount
	};
	constexpr const char* SEAT_NAME[kSeatCount] = { "Behind you", "Second seat behind you", "In front of you", "On your shoulders" };
	constexpr const char* SEAT_WHERE[kSeatCount] = { "behind you", "behind you", "in front of you", "on your shoulders" };

	// Passenger faction rank = which animation set OAR plays on her (see the Ride Along - Seats and Princess Seat folders)
	constexpr std::int8_t RANK_ASTRIDE = 0, RANK_PRINCESS = 1, RANK_FRONT = 2, RANK_SHOULDERS = 3, RANK_GETTING_ON = 10, RANK_GETTING_OFF = 11;

	// ------------------------------------------------------------------ settings
	struct Settings
	{
		std::uint32_t hotkey{ 0x25 };              // DirectX scan code, 0x25 = K
		float         back{ 34.0f };               // how far behind the saddle
		float         up{ -4.0f };                 // height relative to the saddle
		float         side{ 0.0f };                // + = to the horse's right
		float         range{ 1500.0f };            // how close a follower must be
		std::string   anchorNode{ "SaddleBone" };  // horse skeleton node the passengers are pinned to
		std::string   poseEvent{ "HorseEnterInstant" };
		std::string   exitEvent{ "IdleForceDefaultState" };
		bool          noCombat{ true };  // passengers stay out of fights while they ride
		std::string   syncVars{ "Speed,Direction,TurnDelta,SpeedSampled,IsSprinting,bIsSprinting,iSyncSprintState,fSpeedMult,HorseSpeed,TurnDeltaDamped,SpeedDamped" };
		bool          dumpGraphVars{ false };
		bool          copyPose{ true };
		std::string   copyBones{ "Thigh,Calf,Foot,Toe,Pelvis" };
		std::string   skipBones{ "Head,Neck,Eye,Jaw,Tongue,Brow,Lip,Cheek,Mouth,Root,Weapon,Shield,Quiver,Sheath,Magic,Camera,Saddle,Hair" };
		std::string   moveBones{ "" };
		bool          hotkeyEnabled{ true };
		std::uint32_t padButton{ 0 };              // controller button (0 = none); see the list in RideAlong.ini
		float         padHold{ 0.6f };             // seconds the controller button must be held
		bool          dialogueEnabled{ true };     // the "Ride with me." dialogue (needs RideAlong.esp)
		bool          princess{ false };           // female passengers behind you sit sideways (needs the Princess Seat add-on)
		bool          princessCopyPose{ false };
		bool          refreshShoes{ true };        // princess seat: re-wear her shoes after she's set down (high-heels mods)
		float         pBack{ 30.0f };
		float         pUp{ -2.0f };
		float         pSide{ 0.0f };
		float         pTurn{ 90.0f };
		// 0.3: four seats
		bool          seatOn[kSeatCount]{ true, true, true, true };
		float         b2Back{ 62.0f }, b2Up{ -2.0f }, b2Side{ 0.0f };                    // second seat behind you
		float         fBack{ -34.0f }, fUp{ 0.0f }, fSide{ 0.0f }, fTurn{ 0.0f };        // in front of you (negative = ahead of the saddle)
		float         sBack{ 0.0f }, sUp{ 0.0f }, sSide{ 0.0f }, sTurn{ 0.0f };          // on your shoulders
		std::string   shouldersNode{ "NPC Neck [Neck]" };                                // your skeleton node the shoulder seat follows
		// getting on and off (behind-you seats)
		bool          getOnAnim{ true };
		bool          getOffAnim{ true };
		float         getOnSeconds{ 1.65f };       // the game's mount animation is 1.67 s
		float         getOffSeconds{ 2.1f };       // the game's dismount animation is 2.13 s
		float         mBack{ 0.0f }, mUp{ 0.0f }, mSide{ 0.0f };                        // where the climb animation is played, from the horse's origin
		// passenger behaviour
		bool          talkWhileRiding{ true };     // off: passengers can't be talked to (you activate what's in front of you instead)
		bool          ghost{ true };               // your attacks (and everyone else's) pass through passengers
		int           weapons{ 0 };                // 0 = kept sheathed, 1 = bows and crossbows only
		// 0.4: she rides your horse, you ride behind her
		bool          travelOn{ true };            // the "Take me somewhere on my horse." dialogue
		int           travelSpeed{ 2 };            // 0 walk, 1 jog, 2 run
		int           travelCombat{ 0 };           // 0 = keep riding, 1 = stop and get down
		float         arriveDistance{ 900.0f };    // how close to the destination counts as there
		float         driverBack{ 0.0f }, driverUp{ -4.0f };  // her seat, from the saddle
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

	bool B(const std::string& v) { return std::stoi(v) != 0; }

	void LoadSettings()
	{
		std::ifstream in("Data/SKSE/Plugins/RideAlong.ini");
		if (!in) {
			logger::info("RideAlong.ini not found - using defaults");
			return;
		}
		const std::map<std::string, float*> floats{
			{ "BackOffset", &g.back }, { "UpOffset", &g.up }, { "SideOffset", &g.side }, { "Range", &g.range }, { "GamepadHoldSeconds", &g.padHold },
			{ "PrincessBackOffset", &g.pBack }, { "PrincessUpOffset", &g.pUp }, { "PrincessSideOffset", &g.pSide }, { "PrincessTurnDegrees", &g.pTurn },
			{ "Back2BackOffset", &g.b2Back }, { "Back2UpOffset", &g.b2Up }, { "Back2SideOffset", &g.b2Side },
			{ "FrontBackOffset", &g.fBack }, { "FrontUpOffset", &g.fUp }, { "FrontSideOffset", &g.fSide }, { "FrontTurnDegrees", &g.fTurn },
			{ "ShouldersBackOffset", &g.sBack }, { "ShouldersUpOffset", &g.sUp }, { "ShouldersSideOffset", &g.sSide }, { "ShouldersTurnDegrees", &g.sTurn },
			{ "GetOnSeconds", &g.getOnSeconds }, { "GetOffSeconds", &g.getOffSeconds },
			{ "ClimbBackOffset", &g.mBack }, { "ClimbUpOffset", &g.mUp }, { "ClimbSideOffset", &g.mSide },
			{ "TravelArriveDistance", &g.arriveDistance }, { "DriverBackOffset", &g.driverBack }, { "DriverUpOffset", &g.driverUp },
		};
		const std::map<std::string, bool*> bools{
			{ "CopyRiderPose", &g.copyPose }, { "DumpGraphVariables", &g.dumpGraphVars }, { "PeacefulPassenger", &g.noCombat },
			{ "HotkeyEnabled", &g.hotkeyEnabled }, { "DialogueEnabled", &g.dialogueEnabled }, { "PrincessSeat", &g.princess },
			{ "PrincessCopyPose", &g.princessCopyPose }, { "PrincessRefreshShoes", &g.refreshShoes },
			{ "SeatBehind", &g.seatOn[kBack] }, { "SeatBehind2", &g.seatOn[kBack2] }, { "SeatFront", &g.seatOn[kFront] }, { "SeatShoulders", &g.seatOn[kShoulders] },
			{ "GetOnAnimation", &g.getOnAnim }, { "DismountAnimation", &g.getOffAnim }, { "TalkWhileRiding", &g.talkWhileRiding },
			{ "PassengersCantBeHit", &g.ghost }, { "TravelDialogue", &g.travelOn },
		};
		const std::map<std::string, std::string*> strings{
			{ "AnchorNode", &g.anchorNode }, { "PoseEvent", &g.poseEvent }, { "ExitEvent", &g.exitEvent }, { "SyncVariables", &g.syncVars },
			{ "CopyBones", &g.copyBones }, { "SkipBones", &g.skipBones }, { "MoveBones", &g.moveBones }, { "ShouldersAnchorNode", &g.shouldersNode },
		};
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
				if (auto f = floats.find(key); f != floats.end()) {
					*f->second = std::stof(val);
				} else if (auto b = bools.find(key); b != bools.end()) {
					*b->second = B(val);
				} else if (auto s = strings.find(key); s != strings.end()) {
					*s->second = val;
				} else if (key == "Hotkey") {
					g.hotkey = static_cast<std::uint32_t>(std::stoul(val, nullptr, 0));
				} else if (key == "GamepadButton") {
					g.padButton = static_cast<std::uint32_t>(std::stoul(val, nullptr, 0));
				} else if (key == "PassengerWeapons") {
					g.weapons = std::stoi(val) == 1 ? 1 : 0;
				} else if (key == "TravelSpeed") {
					g.travelSpeed = std::clamp(std::stoi(val), 0, 2);
				} else if (key == "TravelInCombat") {
					g.travelCombat = std::stoi(val) == 1 ? 1 : 0;
				}
			} catch (...) {
				logger::warn("RideAlong.ini: bad value for {}: '{}'", key, val);
			}
		}
		logger::info("settings: hotkey=0x{:X} range={} seats={}{}{}{} getOn={} getOff={} talk={} ghost={} weapons={} princess={}",
			g.hotkey, g.range, g.seatOn[0], g.seatOn[1], g.seatOn[2], g.seatOn[3], g.getOnAnim, g.getOffAnim, g.talkWhileRiding, g.ghost,
			g.weapons, g.princess);
	}

	// Writes the settings the in-game menu can change back into RideAlong.ini, keeping every comment and line order.
	void SaveSettings()
	{
		auto f1 = [](float v) { return std::format("{:.1f}", v); };
		auto b = [](bool v) { return std::string(v ? "1" : "0"); };
		const std::map<std::string, std::string> values{
			{ "Hotkey", std::format("0x{:X}", g.hotkey) }, { "HotkeyEnabled", b(g.hotkeyEnabled) },
			{ "GamepadButton", std::format("0x{:X}", g.padButton) }, { "GamepadHoldSeconds", std::format("{:.2f}", g.padHold) },
			{ "DialogueEnabled", b(g.dialogueEnabled) }, { "Range", std::format("{:.0f}", g.range) }, { "PeacefulPassenger", b(g.noCombat) },
			{ "BackOffset", f1(g.back) }, { "UpOffset", f1(g.up) }, { "SideOffset", f1(g.side) },
			{ "SeatBehind", b(g.seatOn[kBack]) }, { "SeatBehind2", b(g.seatOn[kBack2]) }, { "SeatFront", b(g.seatOn[kFront]) },
			{ "SeatShoulders", b(g.seatOn[kShoulders]) },
			{ "Back2BackOffset", f1(g.b2Back) }, { "Back2UpOffset", f1(g.b2Up) }, { "Back2SideOffset", f1(g.b2Side) },
			{ "FrontBackOffset", f1(g.fBack) }, { "FrontUpOffset", f1(g.fUp) }, { "FrontSideOffset", f1(g.fSide) }, { "FrontTurnDegrees", f1(g.fTurn) },
			{ "ShouldersBackOffset", f1(g.sBack) }, { "ShouldersUpOffset", f1(g.sUp) }, { "ShouldersSideOffset", f1(g.sSide) },
			{ "ShouldersTurnDegrees", f1(g.sTurn) },
			{ "GetOnAnimation", b(g.getOnAnim) }, { "DismountAnimation", b(g.getOffAnim) },
			{ "GetOnSeconds", std::format("{:.2f}", g.getOnSeconds) }, { "GetOffSeconds", std::format("{:.2f}", g.getOffSeconds) },
			{ "ClimbBackOffset", f1(g.mBack) }, { "ClimbUpOffset", f1(g.mUp) }, { "ClimbSideOffset", f1(g.mSide) },
			{ "TalkWhileRiding", b(g.talkWhileRiding) }, { "PassengersCantBeHit", b(g.ghost) }, { "PassengerWeapons", std::to_string(g.weapons) },
			{ "PrincessSeat", b(g.princess) }, { "PrincessCopyPose", b(g.princessCopyPose) },
			{ "PrincessBackOffset", f1(g.pBack) }, { "PrincessUpOffset", f1(g.pUp) }, { "PrincessSideOffset", f1(g.pSide) },
			{ "PrincessTurnDegrees", std::format("{:.0f}", g.pTurn) }, { "PrincessRefreshShoes", b(g.refreshShoes) },
			{ "TravelDialogue", b(g.travelOn) }, { "TravelSpeed", std::to_string(g.travelSpeed) }, { "TravelInCombat", std::to_string(g.travelCombat) },
			{ "TravelArriveDistance", std::format("{:.0f}", g.arriveDistance) }, { "DriverBackOffset", f1(g.driverBack) }, { "DriverUpOffset", f1(g.driverUp) },
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

	// ------------------------------------------------------------------ RideAlong.esp: dialogue, passenger faction
	constexpr const char* PLUGIN_FILE = "RideAlong.esp";
	RE::TESFaction* passengerFaction{ nullptr };  // OAR tests its rank: see RANK_*
	RE::TESFaction* currentFollowerFaction{ nullptr };
	RE::TESGlobal*  dialogueGlobal{ nullptr };
	RE::TESGlobal*  seatGlobal[3]{};               // back seats, front, shoulders
	RE::TESTopic*   rideTopic{ nullptr };
	RE::TESTopic*   offTopic{ nullptr };
	RE::TESTopic*   frontTopic{ nullptr };
	RE::TESTopic*   shouldersTopic{ nullptr };
	RE::TESQuest*   dialogueQuest{ nullptr };

	void LoadForms()
	{
		currentFollowerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84E);
		auto dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(PLUGIN_FILE)) {
			logger::info("{} is not active - dialogue and the passenger faction are off (the hotkey still works)", PLUGIN_FILE);
			return;
		}
		dialogueQuest = dh->LookupForm<RE::TESQuest>(0x800, PLUGIN_FILE);
		passengerFaction = dh->LookupForm<RE::TESFaction>(0x802, PLUGIN_FILE);
		dialogueGlobal = dh->LookupForm<RE::TESGlobal>(0x803, PLUGIN_FILE);
		seatGlobal[0] = dh->LookupForm<RE::TESGlobal>(0x809, PLUGIN_FILE);
		seatGlobal[1] = dh->LookupForm<RE::TESGlobal>(0x80A, PLUGIN_FILE);
		seatGlobal[2] = dh->LookupForm<RE::TESGlobal>(0x80B, PLUGIN_FILE);
		rideTopic = dh->LookupForm<RE::TESTopic>(0x810, PLUGIN_FILE);
		offTopic = dh->LookupForm<RE::TESTopic>(0x811, PLUGIN_FILE);
		frontTopic = dh->LookupForm<RE::TESTopic>(0x812, PLUGIN_FILE);
		shouldersTopic = dh->LookupForm<RE::TESTopic>(0x813, PLUGIN_FILE);
		logger::info("{}: quest {} faction {} global {} topics {} {} {} {} seat globals {} {} {}", PLUGIN_FILE, dialogueQuest != nullptr,
			passengerFaction != nullptr, dialogueGlobal != nullptr, rideTopic != nullptr, offTopic != nullptr, frontTopic != nullptr,
			shouldersTopic != nullptr, seatGlobal[0] != nullptr, seatGlobal[1] != nullptr, seatGlobal[2] != nullptr);
	}

	void ApplyDialogueSetting(bool a_startQuest)
	{
		if (dialogueGlobal) dialogueGlobal->value = g.dialogueEnabled ? 1.0f : 0.0f;
		if (seatGlobal[0]) seatGlobal[0]->value = (g.seatOn[kBack] || g.seatOn[kBack2]) ? 1.0f : 0.0f;
		if (seatGlobal[1]) seatGlobal[1]->value = g.seatOn[kFront] ? 1.0f : 0.0f;
		if (seatGlobal[2]) seatGlobal[2]->value = g.seatOn[kShoulders] ? 1.0f : 0.0f;
		if (a_startQuest && dialogueQuest && !dialogueQuest->IsRunning()) {
			dialogueQuest->Start();  // a save made before the plugin was installed
		}
	}

	void SetPassengerTag(RE::Actor* a_actor, bool a_on, std::int8_t a_rank = 0)
	{
		if (!passengerFaction || !a_actor) return;
		if (a_on) {
			a_actor->AddToFaction(passengerFaction, a_rank);  // sets the rank if she's already in it
		} else {
			a_actor->RemoveFromFaction(passengerFaction);
		}
	}

	bool IsFemale(RE::Actor* a_actor)
	{
		auto base = a_actor ? a_actor->GetActorBase() : nullptr;
		return base && base->GetSex() == RE::SEX::kFemale;
	}

	// A follower: a player teammate, or in CurrentFollowerFaction (follower frameworks such as Nether's Follower
	// Framework keep their followers there).
	bool IsFollower(RE::Actor* a_actor)
	{
		if (!a_actor) return false;
		if (a_actor->IsPlayerTeammate()) return true;
		return currentFollowerFaction && a_actor->IsInFaction(currentFollowerFaction) && a_actor->GetFactionRank(currentFollowerFaction, false) >= 0;
	}

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

	std::vector<GraphVar> ListGraphVars(RE::Actor* a_actor)
	{
		std::vector<GraphVar>                            out;
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
			if (!n) continue;
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
		const auto  vars = ListGraphVars(a_actor);
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
			if (val.empty() || val == "0" || val == "0.000") continue;
			line += std::format("{}={} ", v.name.c_str(), val);
		}
		logger::info("graph vars [{}] {} ({} total, non-zero shown): {}", a_tag, a_actor->GetDisplayFullName(), vars.size(), line);
	}

	bool ListHas(const std::string& a_list, std::string_view a_name, bool a_substring)
	{
		std::size_t start = 0;
		while (start <= a_list.size()) {
			auto comma = a_list.find(',', start);
			if (comma == std::string::npos) comma = a_list.size();
			const auto item = Trim(a_list.substr(start, comma - start));
			if (!item.empty() && (a_substring ? a_name.find(item) != std::string_view::npos : a_name == item)) {
				return true;
			}
			start = comma + 1;
		}
		return false;
	}

	// ------------------------------------------------------------------ passengers
	struct BonePair
	{
		RE::NiAVObject* to;
		RE::NiAVObject* from;
		bool            move;
	};

	enum class Phase
	{
		kNone,
		kGettingOn,   // the climb-on animation plays beside the horse
		kSeated,
		kGettingOff   // the climb-off animation plays, then she's set down
	};

	struct Rider
	{
		RE::ActorHandle       h;
		Phase                 phase{ Phase::kNone };
		bool                  princess{ false };
		std::uint32_t         phaseStart{ 0 };
		bool                  announce{ false };
		bool                  finishing{ false };
		bool                  posePending{ false };
		std::atomic<bool>     have{ false };  // seatPos is valid (read by the movement hook)
		RE::NiPoint3          seatPos{};
		float                 seatYaw{ 0.0f };
		std::uint32_t         frames{ 0 };
		std::uint32_t         poseResends{ 0 };
		std::uint32_t         combatStops{ 0 };
		std::vector<BonePair> bones;
		RE::NiAVObject*       bonesTo{ nullptr };
		RE::NiAVObject*       bonesFrom{ nullptr };
		// restored when she gets off
		RE::FormID            wRight{ 0 }, wLeft{ 0 };
		bool                  weaponsChanged{ false };
		bool                  ghostSet{ false };
		bool                  talkOff{ false };

		void Clear()
		{
			have = false;
			h.reset();
			phase = Phase::kNone;
			princess = announce = finishing = posePending = weaponsChanged = ghostSet = talkOff = false;
			phaseStart = frames = poseResends = combatStops = 0;
			bones.clear();
			bonesTo = bonesFrom = nullptr;
			wRight = wLeft = 0;
		}
	};
	std::array<Rider, kSeatCount> riders;
	RE::ActorHandle               horseH;
	std::uint32_t                 cellMoveTime{ 0 };
	std::uint32_t                 boardTime{ 0 };
	bool                          dumpedMoving{ false };
	bool                          alightQueued{ false };
	bool                          padFired{ false };

	struct Pending
	{
		RE::ActorHandle h;
		int             want;  // a Seat, or -1 = any seat behind you
	};
	std::vector<Pending> pending;  // agreed to ride (in dialogue) while you were on foot
	bool                 pendingQueued{ false };
	RE::ActorHandle      askedH;  // chosen in dialogue, handled when the dialogue closes
	int                  askedWant{ -1 };
	bool                 askedOff{ false };
	bool                 asked{ false };

	bool AnyRiding()
	{
		for (auto& r : riders) {
			if (r.h) return true;
		}
		return false;
	}

	Rider* RiderOf(RE::Actor* a_actor)
	{
		if (!a_actor) return nullptr;
		for (auto& r : riders) {
			if (r.h && r.h.get().get() == a_actor) return &r;
		}
		return nullptr;
	}

	int SeatOf(const Rider* a_rider) { return a_rider ? static_cast<int>(a_rider - riders.data()) : -1; }

	// the first free, switched-on seat of the kind asked for (-1 = any seat behind you, -2 = any seat at all)
	int FreeSeat(int a_want)
	{
		std::vector<int> order;
		if (a_want == -1) order = { kBack, kBack2 };
		else if (a_want == -2) order = { kBack, kBack2, kFront, kShoulders };
		else order = { a_want };
		for (int s : order) {
			if (g.seatOn[s] && !riders[s].h) return s;
		}
		return -1;
	}

	std::int8_t SeatRank(const Rider& r)
	{
		switch (SeatOf(&r)) {
		case kFront:
			return RANK_FRONT;
		case kShoulders:
			return RANK_SHOULDERS;
		default:
			return r.princess ? RANK_PRINCESS : RANK_ASTRIDE;
		}
	}

	bool ClimbAnimated(int a_seat) { return a_seat == kBack || a_seat == kBack2; }  // the game's climb animations fit the seats behind you

	// ------------------------------------------------------------------ rider pose
	// The rider's clips are synchronized to the horse they sit on, so on a passenger (who has no horse of its own)
	// they never advance. Instead the passenger's legs copy the player's every frame.
	void CollectBones(Rider& r, RE::NiAVObject* a_obj, RE::NiAVObject* a_fromRoot)
	{
		if (!a_obj) return;
		const char* n = a_obj->name.c_str();
		if (n && std::string_view(n).starts_with("NPC ") && (g.copyBones.empty() || ListHas(g.copyBones, n, true)) && !ListHas(g.skipBones, n, true)) {
			if (auto from = a_fromRoot->GetObjectByName(a_obj->name)) {
				r.bones.push_back({ a_obj, from, ListHas(g.moveBones, n, false) });
			}
		}
		if (auto node = a_obj->AsNode()) {
			for (auto& child : node->GetChildren()) {
				if (child) CollectBones(r, child.get(), a_fromRoot);
			}
		}
	}

	void CopyPose(Rider& r, RE::Actor* a_passenger, RE::Actor* a_rider)
	{
		auto to = a_passenger->Get3D(false);
		auto from = a_rider->Get3D(false);
		if (!to || !from) return;
		if (to != r.bonesTo || from != r.bonesFrom) {
			r.bones.clear();
			CollectBones(r, to, from);
			r.bonesTo = to;
			r.bonesFrom = from;
			logger::info("pose copy: {} bones linked", r.bones.size());
		}
		for (const auto& b : r.bones) {
			b.to->local.rotate = b.from->local.rotate;
			if (b.move) b.to->local.translate = b.from->local.translate;
		}
		RE::NiUpdateData ctx{};
		for (const auto& b : r.bones) b.to->Update(ctx);
	}

	std::vector<GraphVar> syncList;

	void BuildSyncList(RE::Actor* a_from)
	{
		syncList.clear();
		const auto  all = ListGraphVars(a_from);
		std::string wanted = g.syncVars + ",";
		std::size_t start = 0;
		std::string found, missing;
		while (true) {
			const auto comma = wanted.find(',', start);
			if (comma == std::string::npos) break;
			const auto name = Trim(wanted.substr(start, comma - start));
			start = comma + 1;
			if (name.empty()) continue;
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

	void ResetAll()
	{
		for (auto& r : riders) r.Clear();
		horseH.reset();
		cellMoveTime = 0;
		alightQueued = false;
	}

	// Papyrus MoveTo: moves a reference and rebuilds its physics (gives the passenger its character controller back)
	void MoveTo(RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_target, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_rot)
	{
		using func_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
		static REL::Relocation<func_t> func{ RELOCATION_ID(56227, 56626) };
		const RE::ObjectRefHandle      handle = a_target->RE::TESObjectREFR::GetHandle();
		func(a_ref, handle, a_target->GetParentCell(), a_target->GetWorldspace(), a_pos, a_rot);
	}

	// Calm: she won't start fights or join yours (Aggression 0 = unaggressive, Assistance 0 = helps nobody).
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

	// Ghost: attacks pass through her (the same flag Papyrus SetGhost sets), so your sword swings don't hit the
	// passenger sitting right behind you - with or without Precision.
	void SetGhost(RE::Actor* a_actor, bool a_on)
	{
		auto x = a_actor->extraList.GetByType<RE::ExtraGhost>();
		if (!x) {
			if (!a_on) return;
			x = RE::BSExtraData::Create<RE::ExtraGhost>();
			if (!x) return;
			x->ghost = false;
			a_actor->extraList.Add(x);
		}
		x->ghost = a_on;
	}

	bool IsRanged(RE::TESForm* a_form)
	{
		auto w = a_form ? a_form->As<RE::TESObjectWEAP>() : nullptr;
		return w && (w->IsBow() || w->IsCrossbow());
	}

	// "Bows and crossbows only": melee weapons are put away for the ride and a bow from her pack is equipped if she has
	// one. What she was holding goes back on when she gets off.
	void PrepareWeapons(Rider& r, RE::Actor* a_actor)
	{
		if (g.weapons != 1) return;
		auto right = a_actor->GetEquippedObject(false);
		auto left = a_actor->GetEquippedObject(true);
		if (IsRanged(right)) return;  // already holding one
		r.wRight = right ? right->GetFormID() : 0;
		r.wLeft = left ? left->GetFormID() : 0;
		auto mgr = RE::ActorEquipManager::GetSingleton();
		RE::TESObjectWEAP* bow = nullptr;
		for (auto& [obj, data] : a_actor->GetInventory()) {
			if (data.first > 0 && IsRanged(obj)) {
				bow = obj->As<RE::TESObjectWEAP>();
				break;
			}
		}
		for (auto form : { right, left }) {
			if (auto bound = form ? form->As<RE::TESBoundObject>() : nullptr; bound && (form->Is(RE::FormType::Weapon) || form->Is(RE::FormType::Armor))) {
				mgr->UnequipObject(a_actor, bound, nullptr, 1, nullptr, false, false, false, true);
			}
		}
		if (bow) mgr->EquipObject(a_actor, bow, nullptr, 1, nullptr, false, false, false, true);
		r.weaponsChanged = true;
		logger::info("{}: weapons swapped for the ride ({})", a_actor->GetDisplayFullName(), bow ? bow->GetName() : "no bow - hands empty");
	}

	void RestoreWeapons(Rider& r, RE::Actor* a_actor)
	{
		if (!r.weaponsChanged) return;
		auto mgr = RE::ActorEquipManager::GetSingleton();
		auto now = a_actor->GetEquippedObject(false);
		if (IsRanged(now) && now->GetFormID() != r.wRight) {
			mgr->UnequipObject(a_actor, now->As<RE::TESBoundObject>(), nullptr, 1, nullptr, false, false, false, true);
		}
		auto dom = RE::BGSDefaultObjectManager::GetSingleton();
		auto leftSlot = dom ? dom->GetObject<RE::BGSEquipSlot>(static_cast<std::size_t>(RE::DEFAULT_OBJECTS::kLeftHandEquip)) : nullptr;
		auto rightSlot = dom ? dom->GetObject<RE::BGSEquipSlot>(static_cast<std::size_t>(RE::DEFAULT_OBJECTS::kRightHandEquip)) : nullptr;
		for (auto [id, slot] : { std::pair{ r.wRight, rightSlot }, std::pair{ r.wLeft, leftSlot } }) {
			auto item = id ? RE::TESForm::LookupByID<RE::TESBoundObject>(id) : nullptr;
			if (!item) continue;
			auto inv = a_actor->GetInventory([item](RE::TESBoundObject& o) { return &o == item; });
			if (inv.empty()) continue;
			mgr->EquipObject(a_actor, item, nullptr, 1, item->Is(RE::FormType::Armor) ? nullptr : slot, false, false, false, true);
		}
		r.weaponsChanged = false;
	}

	// Keeps her out of anything that still reaches her. Her weapon draw itself is blocked by the draw hook below.
	void KeepPeaceful(Rider& r, RE::Actor* a_passenger)
	{
		if (a_passenger->IsInCombat()) {
			a_passenger->StopCombat();
			if (r.combatStops++ % 200 == 0) {
				logger::info("{}: kept out of the fight ({}x this ride)", a_passenger->GetDisplayFullName(), r.combatStops);
			}
		}
	}

	bool OnTrip(RE::Actor* a_actor);  // 0.4 trip, below

	// Whether a seated passenger may draw her weapon (the draw hook asks this).
	bool MayDraw(RE::Actor* a_actor)
	{
		if (OnTrip(a_actor)) return false;  // she drives, you ride: nobody fights from the horse
		auto r = RiderOf(a_actor);
		if (!r || r->phase == Phase::kNone) return true;
		if (r->phase != Phase::kSeated) return false;  // never while climbing on or off
		if (g.noCombat) return false;
		return g.weapons == 1 ? IsRanged(a_actor->GetEquippedObject(false)) : true;
	}

	// After loading nobody is riding: anyone a save caught on a seat gets back what the ride changed.
	void ReleaseSavedPassengers()
	{
		if (!passengerFaction) return;
		for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
			auto a = h.get();
			if (a && a->IsInFaction(passengerFaction)) {
				SetPassengerTag(a.get(), false);
				SetCalm(a.get(), false);
				SetGhost(a.get(), false);
				a->AllowPCDialogue(true);
				a->EnableAI(true);
				a->EvaluatePackage(true, false);
				logger::info("{}: released (saved while riding)", a->GetDisplayFullName());
			}
		}
	}

	// ------------------------------------------------------------------ riding
	RE::NiPoint3 Rotate(float a_yaw, const RE::NiPoint3& a_local)  // x = right, y = forward, z = up
	{
		const float s = std::sin(a_yaw), c = std::cos(a_yaw);
		return { a_local.x * c + a_local.y * s, -a_local.x * s + a_local.y * c, a_local.z };
	}

	bool Place(Rider& r, RE::Actor* a_passenger, RE::Actor* a_horse, RE::Actor* a_player)
	{
		auto root = a_horse->Get3D();
		if (!root || !a_passenger->Get3D()) return false;
		const int    seat = SeatOf(&r);
		const float  hyaw = a_horse->data.angle.z;
		RE::NiPoint3 pos;
		float        yaw = hyaw;
		if (r.phase == Phase::kGettingOn || r.phase == Phase::kGettingOff) {
			// The climb clips are the rider's half of the game's paired mount/dismount clips. Their "NPC" bone (where
			// the rider stands relative to the horse) isn't part of a solo clip, so it's applied here instead:
			// climbing on starts 63.5 units to the horse's left, facing it; climbing off starts on the saddle.
			const bool  on = r.phase == Phase::kGettingOn;
			const float clipSide = on ? -63.48f : 1.67f, clipFwd = on ? 0.0f : 4.42f, clipUp = on ? 0.0f : 130.44f;
			const float seatBack = seat == kBack2 ? g.b2Back : g.back;  // she ends up (or starts) on her own seat
			pos = a_horse->data.location + Rotate(hyaw, { clipSide + g.mSide, clipFwd - (seatBack + g.mBack), clipUp + g.mUp });
			if (on) yaw = hyaw + 1.5707963f;  // the clip turns her a quarter right, to face the horse's flank
		} else if (seat == kShoulders) {
			auto proot = a_player->Get3D();
			auto node = proot ? proot->GetObjectByName(RE::BSFixedString(g.shouldersNode.c_str())) : nullptr;
			const RE::NiPoint3 base = node ? node->world.translate : a_player->data.location + RE::NiPoint3{ 0, 0, 120 };
			pos = base + Rotate(hyaw, { g.sSide, -g.sBack, g.sUp });
			yaw = hyaw + g.sTurn * 0.01745329f;
		} else {
			static RE::BSFixedString anchorName(g.anchorNode.c_str());
			RE::NiAVObject*          anchor = root->GetObjectByName(anchorName);
			const RE::NiPoint3       base = anchor ? anchor->world.translate : root->world.translate;
			RE::NiPoint3             offset;
			if (seat == kFront) {
				offset = { g.fSide, -g.fBack, g.fUp };
				yaw = hyaw + g.fTurn * 0.01745329f;
			} else if (r.princess) {
				offset = { g.pSide, -(g.pBack + (seat == kBack2 ? g.b2Back - g.back : 0.0f)), g.pUp };
				yaw = hyaw + g.pTurn * 0.01745329f;
			} else if (seat == kBack2) {
				offset = { g.b2Side, -g.b2Back, g.b2Up };
			} else {
				offset = { g.side, -g.back, g.up };
			}
			pos = base + root->world.rotate * offset;
		}
		if (a_passenger->GetCharController()) {
			a_passenger->DetachCharController();  // no capsule: nothing to collide with the horse, no AI walking
		}
		a_passenger->data.location = pos;
		r.seatPos = pos;
		r.seatYaw = yaw;
		r.have = true;
		a_passenger->data.angle = RE::NiPoint3{ 0.0f, 0.0f, yaw };
		a_passenger->Update3DPosition(true);
		if (r.frames++ % 600 == 0) {
			logger::info("riding [{}]: pos=({:.0f},{:.0f},{:.0f})", SEAT_NAME[seat], pos.x, pos.y, pos.z);
		}
		return true;
	}

	// The side-saddle animations are yukl's (YY Anim Replacer - Princess' Horse Riding) and are not shipped with
	// Ride Along: the player copies them from that mod into the Princess Seat add-on's OAR folder.
	bool PrincessAnimsInstalled()
	{
		static int state = -1;
		if (state < 0) {
			std::error_code ec;
			state = std::filesystem::exists("Data/meshes/actors/character/animations/OpenAnimationReplacer/RideAlongPrincess/Passenger/horse_rider/idle.hkx", ec) ? 1 : 0;
			logger::info("princess seat animations: {}", state ? "found" : "NOT found - see the Princess Seat readme");
		}
		return state == 1;
	}

	bool SeatAnimsInstalled()
	{
		static int state = -1;
		if (state < 0) {
			std::error_code ec;
			state = std::filesystem::exists("Data/meshes/actors/character/animations/OpenAnimationReplacer/RideAlongSeats/Front (hug)/horse_rider/idle.hkx", ec) ? 1 : 0;
			logger::info("seat animations (front, shoulders, getting on and off): {}", state ? "found" : "NOT found - is the Ride Along OAR folder installed?");
		}
		return state == 1;
	}

	void Board(RE::Actor* a_passenger, RE::Actor* a_horse, int a_seat)
	{
		auto& r = riders[a_seat];
		r.Clear();
		r.h = a_passenger->GetHandle();
		horseH = a_horse->GetHandle();
		alightQueued = false;
		r.princess = (a_seat == kBack || a_seat == kBack2) && g.princess && IsFemale(a_passenger) && PrincessAnimsInstalled();
		const bool climb = g.getOnAnim && ClimbAnimated(a_seat) && SeatAnimsInstalled();
		r.phase = climb ? Phase::kGettingOn : Phase::kSeated;
		r.phaseStart = RE::GetDurationOfApplicationRunTime();
		SetPassengerTag(a_passenger, true, climb ? RANK_GETTING_ON : SeatRank(r));  // before the pose event: OAR sees it on the first clip
		if (g.noCombat) {
			a_passenger->StopCombat();
			SetCalm(a_passenger, true);
		}
		a_passenger->DrawWeaponMagicHands(false);  // weapon away for the ride
		PrepareWeapons(r, a_passenger);
		if (g.ghost) {
			SetGhost(a_passenger, true);
			r.ghostSet = true;
		}
		if (!g.talkWhileRiding) {
			a_passenger->AllowPCDialogue(false);  // no prompt on her: you activate what's in front of you
			r.talkOff = true;
		}
		a_passenger->DetachCharController();
		auto player = RE::PlayerCharacter::GetSingleton();
		Place(r, a_passenger, a_horse, player);
		const bool ok = a_passenger->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
		logger::info("board [{}]: {} -> pose event '{}' {}{}", SEAT_NAME[a_seat], a_passenger->GetDisplayFullName(), g.poseEvent,
			ok ? "accepted" : "REJECTED", climb ? ", climbing on" : "");
		BuildSyncList(player);
		boardTime = RE::GetDurationOfApplicationRunTime();
		dumpedMoving = false;
		if (g.dumpGraphVars) {
			DumpGraphVars(player, "player, on board");
			DumpGraphVars(a_passenger, "passenger, on board");
		}
		Notify(std::format("{} rides {}.", a_passenger->GetDisplayFullName(), SEAT_WHERE[a_seat]));
	}

	// High-heels mods drop the heel height while she rides; taking her shoes off and on makes them apply it again.
	RE::ExtraDataList* ListFor(RE::Actor* a_actor, RE::TESObjectARMO* a_item, bool a_worn)
	{
		auto inv = a_actor->GetInventory([a_item](RE::TESBoundObject& o) { return &o == a_item; });
		for (auto& [obj, data] : inv) {
			auto& entry = data.second;
			if (!entry || !entry->extraLists) continue;
			for (auto* l : *entry->extraLists) {
				if (l && (!a_worn || l->HasType(RE::ExtraDataType::kWorn))) return l;
			}
		}
		return nullptr;
	}

	void RefreshShoes(RE::Actor* a_actor)
	{
		auto feet = a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kFeet);
		if (!feet) return;
		auto mgr = RE::ActorEquipManager::GetSingleton();
		mgr->UnequipObject(a_actor, feet, ListFor(a_actor, feet, true), 1, nullptr, true, false, false, false);
		const RE::ActorHandle h = a_actor->GetHandle();
		const RE::FormID      id = feet->GetFormID();
		std::thread([h, id] {
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			SKSE::GetTaskInterface()->AddTask([h, id] {
				auto a = h.get();
				auto shoes = RE::TESForm::LookupByID<RE::TESObjectARMO>(id);
				if (!a || !shoes || a->IsDead()) return;
				if (a->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kFeet)) return;
				RE::ActorEquipManager::GetSingleton()->EquipObject(a.get(), shoes, ListFor(a.get(), shoes, false), 1, nullptr, true, false, false, false);
				logger::info("{}: shoes re-worn after the side-saddle dismount", a->GetDisplayFullName());
			});
		}).detach();
	}

	// where each seat's passenger is set down, from the horse (x = right, y = forward)
	RE::NiPoint3 SetDownSpot(int a_seat)
	{
		switch (a_seat) {
		case kBack2:
			return { -110.0f, -70.0f, 0.0f };
		case kFront:
			return { -110.0f, 70.0f, 0.0f };
		case kShoulders:
			return { 110.0f, 0.0f, 0.0f };
		default:
			return { -110.0f, 0.0f, 0.0f };  // the horse's left
		}
	}

	void FinishAlight(int a_seat, bool a_announce)
	{
		auto& r = riders[a_seat];
		auto  passenger = r.h.get();
		auto  horse = horseH.get();
		const bool shoes = r.princess;
		if (passenger) {
			SetCalm(passenger.get(), false);
			if (r.ghostSet) SetGhost(passenger.get(), false);
			if (r.talkOff) passenger->AllowPCDialogue(true);
			RestoreWeapons(r, passenger.get());
		}
		r.Clear();
		if (!AnyRiding()) horseH.reset();
		if (!passenger) return;
		// Leave the passenger tag on for a moment: changing it swaps the OAR animation set while the graph is still
		// leaving the riding state, which can crash the game. Drop it once she's on her feet.
		{
			RE::ActorHandle h = passenger->GetHandle();
			std::thread([h] {
				std::this_thread::sleep_for(std::chrono::milliseconds(2000));
				SKSE::GetTaskInterface()->AddTask([h] {
					auto a = h.get();
					if (a && !RiderOf(a.get())) SetPassengerTag(a.get(), false);  // unless she climbed back on
				});
			}).detach();
		}
		// End the riding animation now; the move (which rebuilds her physics) a few frames later - doing both in the
		// same frame crashed the game in the cloth/hair physics (FSMP).
		passenger->NotifyAnimationGraph(RE::BSFixedString(g.exitEvent.c_str()));
		logger::info("alight [{}]: {}", SEAT_NAME[a_seat], passenger->GetDisplayFullName());
		if (a_announce) Notify(std::format("{} gets off.", passenger->GetDisplayFullName()));
		const RE::ActorHandle ph = passenger->GetHandle();
		const RE::ActorHandle hh = horse ? horse->GetHandle() : RE::ActorHandle{};
		const RE::NiPoint3    local = SetDownSpot(a_seat);
		std::thread([ph, hh, shoes, local] {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			SKSE::GetTaskInterface()->AddTask([ph, hh, shoes, local] {
				auto p = ph.get();
				if (!p || RiderOf(p.get()) || p->IsDead() || !p->Is3DLoaded()) return;
				auto               h = hh.get();
				RE::Actor*         anchor = (h && !h->IsDead()) ? h.get() : RE::PlayerCharacter::GetSingleton();
				const float        a = anchor->data.angle.z;
				const RE::NiPoint3 spot = anchor->data.location + Rotate(a, local);
				MoveTo(p.get(), anchor, spot, RE::NiPoint3{ 0.0f, 0.0f, a });
				std::thread([ph, shoes] {
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
					SKSE::GetTaskInterface()->AddTask([ph, shoes] {
						auto p = ph.get();
						if (!p || RiderOf(p.get()) || p->IsDead()) return;
						p->EvaluatePackage(true, false);
						if (shoes && g.refreshShoes) RefreshShoes(p.get());
					});
				}).detach();
			});
		}).detach();
	}

	// Getting off: the climb-off animation plays beside the horse first (she stays pinned there while it does),
	// then she is set down.
	void Alight(int a_seat, bool a_announce)
	{
		auto& r = riders[a_seat];
		if (!r.h || r.phase == Phase::kGettingOff || r.finishing) return;
		auto passenger = r.h.get();
		auto horse = horseH.get();
		if (g.getOffAnim && ClimbAnimated(a_seat) && SeatAnimsInstalled() && passenger && horse && !horse->IsDead() && !passenger->IsDead()) {
			r.phase = Phase::kGettingOff;
			r.phaseStart = RE::GetDurationOfApplicationRunTime();
			r.announce = a_announce;
			SetPassengerTag(passenger.get(), true, RANK_GETTING_OFF);
			passenger->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));  // restart the clip from its first frame
			logger::info("getting off [{}]: {:.1f}s", SEAT_NAME[a_seat], g.getOffSeconds);
			return;
		}
		r.finishing = true;
		FinishAlight(a_seat, a_announce);
	}

	void AlightAll(bool a_announce)
	{
		for (int s = 0; s < kSeatCount; ++s) {
			if (riders[s].h) Alight(s, a_announce);
		}
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
			if (!a || a == a_player || a == a_horse || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded() || RiderOf(a) ||
				!IsFollower(a) || a->IsOnMount() || a->IsAMount() || !IsHumanoid(a)) {
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

	bool Busy()
	{
		for (auto& r : riders) {
			if (r.h && (r.phase == Phase::kGettingOn || r.phase == Phase::kGettingOff)) return true;
		}
		return false;
	}

	// The key: seats the nearest follower who isn't riding yet in the next free seat; when nobody else can get on,
	// everyone gets off.
	void EndTrip(const char* a_why);
	bool TripActive();

	void Toggle()
	{
		if (TripActive()) {
			EndTrip("You get down.");
			return;
		}
		if (Busy()) return;
		auto                     player = RE::PlayerCharacter::GetSingleton();
		RE::NiPointer<RE::Actor> horse;
		if (!player->IsOnMount() || !player->GetMount(horse) || !horse) {
			if (AnyRiding()) AlightAll(true);
			else Notify("Mount your horse first.");
			return;
		}
		auto      follower = FindFollower(player, horse.get());
		const int seat = FreeSeat(-2);
		if (follower && seat >= 0) {
			Board(follower.get(), horse.get(), seat);
		} else if (AnyRiding()) {
			AlightAll(true);
		} else if (!follower) {
			Notify("No follower is close enough to ride with you.");
		} else {
			Notify("Every seat is switched off in the Ride Along settings.");
		}
	}

	// ------------------------------------------------------------------ 0.4: she rides, you ride behind her
	// "Take me somewhere." -> a destination: your follower sits in the saddle of your horse, you sit behind her, and the
	// horse itself is given a travel package to the destination. Nobody really "rides" it: the horse walks the roads on its
	// own AI and both of you are pinned to it every frame, exactly like passengers.
	struct Destination
	{
		const char* name;
		RE::FormID  marker;            // map marker in Tamriel (the big cities: their stables); 0 = your custom map marker
		const char* file{ nullptr };   // nullptr = Skyrim.esm (marker is a full form ID), else the plugin it's in
	};
	// same order as DESTS in esp_tools/mkesp.py (dialogue topic 0x820 + index), grouped by hold there
	constexpr Destination DESTINATIONS[] = {
		{ "Whiterun", 0x00072879 }, { "Riverwood", 0x000162A4 }, { "Rorikstead", 0x000177CC },                          // Whiterun Hold
		{ "Solitude", 0x0001635E }, { "Dragon Bridge", 0x00017753 },                                                       // Haafingar
		{ "Windhelm", 0x0009846F }, { "Kynesgrove", 0x000177A1 }, { "Darkwater Crossing", 0x00017732 },
		{ "Mixwater Mill", 0x0001636D },                                                                                   // Eastmarch
		{ "Riften", 0x00096A46 }, { "Ivarstead", 0x00017791 }, { "Shor's Stone", 0x000177D7 }, { "Heartwood Mill", 0x0001634F },  // The Rift
		{ "Markarth", 0x000E962F }, { "Karthwasten", 0x0001779A }, { "Old Hroldan", 0x000177C3 }, { "Salvius Farm", 0x00016376 },
		{ "Soljund's Sinkhole", 0x000162BB }, { "Kolskeggr Mine", 0x0002051D },                                            // The Reach
		{ "Falkreath", 0x00017760 }, { "Helgen", 0x00017780 }, { "Half-Moon Mill", 0x00016370 },
		{ "Lakeview Manor", 0x0030AA, "HearthFires.esm" },                                                                 // Falkreath Hold
		{ "Morthal", 0x000177B0 }, { "Stonehills", 0x000177E4 }, { "Windstad Manor", 0x00B8B0, "HearthFires.esm" },        // Hjaalmarch
		{ "Dawnstar", 0x0001773A }, { "Nightgate Inn", 0x00017785 }, { "Heljarchen Hall", 0x010DE8, "HearthFires.esm" },   // The Pale
		{ "Winterhold", 0x000177EF },                                                                                      // Winterhold
		{ "the place on your map", 0 } };
	constexpr int          DEST_COUNT = static_cast<int>(std::size(DESTINATIONS));
	constexpr std::int8_t  RANK_DRIVER = 20;  // passenger faction rank while she drives (the "Stop here." line tests it)
	using UEF = RE::ControlMap::UEFlag;
	constexpr UEF TRIP_CONTROLS = static_cast<UEF>(static_cast<std::uint32_t>(UEF::kMovement) | static_cast<std::uint32_t>(UEF::kFighting) |
												   static_cast<std::uint32_t>(UEF::kSneaking) | static_cast<std::uint32_t>(UEF::kJumping));

	RE::TESGlobal* travelGlobal{ nullptr };
	RE::TESTopic*  travelTopic{ nullptr };
	RE::TESTopic*  stopTopic{ nullptr };
	RE::TESTopic*  destTopic[DEST_COUNT]{};

	struct Trip
	{
		bool                 active{ false };
		bool                 ending{ false };
		bool                 endQueued{ false };  // EndTrip is on its way (queued from the frame update)
		RE::ActorHandle      driver;
		RE::ActorHandle      horse;
		RE::ObjectRefHandle  target;
		std::string          where;
		RE::TESPackage*      package{ nullptr };
		bool                 controlsOff{ false };
		std::atomic<bool>    have{ false };  // the positions below are valid (read by the movement hook)
		RE::NiPoint3         driverPos{}, playerPos{};
		float                yaw{ 0.0f };
		std::uint32_t        start{ 0 }, lastCheck{ 0 }, frames{ 0 };
		RE::NiPoint3         lastHorsePos{};
		int                  stuck{ 0 };
		int                  nudges{ 0 };
		int                  idleChecks{ 0 };  // checks in a row the horse wasn't running the travel package
		bool                 ghostSet{ false };
	};
	Trip trip;
	RE::ActorHandle tripAskedH;   // chosen in dialogue, handled when the dialogue closes
	int             tripAskedDest{ -1 };
	bool            tripAskedStop{ false };

	bool OnTrip(RE::Actor* a_actor)
	{
		if (!trip.active || !a_actor) return false;
		return a_actor->IsPlayerRef() || trip.driver.get().get() == a_actor;
	}

	RE::TESObjectREFR* DestinationRef(int a_dest)
	{
		if (a_dest < 0 || a_dest >= DEST_COUNT) return nullptr;
		const auto& d = DESTINATIONS[a_dest];
		if (d.marker && d.file) {
			auto dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm<RE::TESObjectREFR>(d.marker, d.file) : nullptr;  // Hearthfire homes
		}
		if (d.marker) return RE::TESForm::LookupByID<RE::TESObjectREFR>(d.marker);
		auto player = RE::PlayerCharacter::GetSingleton();
		return player->GetInfoRuntimeData().playerMapMarker.get().get();
	}

	// The horse's travel package is a record in RideAlong.esp (0x8F0, built on the game's Travel template). Making
	// one at runtime gave a package with no procedure, which crashed the horse's AI. Its destination is written into
	// the record's location data before each trip, and set back to "near itself" afterwards.
	RE::PackageLocation* TravelLocation(RE::TESPackage* a_pkg)
	{
		auto custom = a_pkg ? skyrim_cast<RE::TESCustomPackageData*>(a_pkg->data) : nullptr;
		if (!custom || !custom->data.data) return nullptr;
		for (std::uint16_t i = 0; i < custom->data.dataSize; ++i) {
			auto item = custom->data.data[i];
			if (auto loc = item ? skyrim_cast<RE::BGSPackageDataLocation*>(item) : nullptr; loc && loc->pointer) return loc->pointer;
		}
		return nullptr;
	}

	RE::TESPackage* MakeTravelPackage(RE::TESObjectREFR* a_target)
	{
		auto dh = RE::TESDataHandler::GetSingleton();
		auto pkg = dh ? dh->LookupForm<RE::TESPackage>(0x8F0, PLUGIN_FILE) : nullptr;
		auto loc = TravelLocation(pkg);
		if (!pkg || !loc) {
			logger::warn("travel package: record {} location {} - is RideAlong.esp up to date?", pkg != nullptr, loc != nullptr);
			return nullptr;
		}
		loc->locType = RE::PackageLocation::Type::kNearReference;
		loc->rad = static_cast<std::uint32_t>(g.arriveDistance * 0.5f);
		loc->data.refHandle = a_target->GetHandle();
		static constexpr RE::PACKAGE_DATA::PreferredSpeed SPEEDS[] = { RE::PACKAGE_DATA::PreferredSpeed::kWalk,
			RE::PACKAGE_DATA::PreferredSpeed::kJog, RE::PACKAGE_DATA::PreferredSpeed::kRun };
		pkg->packData.maxSpeed = SPEEDS[std::clamp(g.travelSpeed, 0, 2)];
		if (g.travelCombat == 0) pkg->packData.packFlags.set(RE::PACKAGE_DATA::GeneralFlag::kIgnoreCombat);
		else pkg->packData.packFlags.reset(RE::PACKAGE_DATA::GeneralFlag::kIgnoreCombat);
		logger::info("travel package {:08X}: type {} procedure {} -> {:08X} radius {}", pkg->GetFormID(), static_cast<int>(pkg->packData.packType.get()),
			static_cast<int>(pkg->procedureType.get()), a_target->GetFormID(), loc->rad);
		return pkg;
	}

	void ResetTravelPackage()
	{
		auto dh = RE::TESDataHandler::GetSingleton();
		auto pkg = dh ? dh->LookupForm<RE::TESPackage>(0x8F0, PLUGIN_FILE) : nullptr;
		if (auto loc = TravelLocation(pkg)) {
			loc->locType = RE::PackageLocation::Type::kNearSelf;
			loc->data.refHandle = RE::ObjectRefHandle{};
			loc->rad = 500;
		}
	}

	// Package overrides through PapyrusUtil (ActorUtil.AddPackageOverride), the way most mods hand an actor a package:
	// a package pushed straight into the horse's AI (PutCreatedPackage) was never picked up - the horse stood still.
	bool CallActorUtil(const char* a_fn, RE::Actor* a_actor, RE::TESPackage* a_pkg, bool a_add)
	{
		auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm || !a_actor || !a_pkg || !vm->TypeIsValid(RE::BSFixedString("ActorUtil"))) return false;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
		RE::BSScript::IFunctionArguments* args = a_add ?
			RE::MakeFunctionArguments(static_cast<RE::Actor*>(a_actor), static_cast<RE::TESPackage*>(a_pkg), static_cast<std::int32_t>(100), static_cast<std::int32_t>(1)) :
			RE::MakeFunctionArguments(static_cast<RE::Actor*>(a_actor), static_cast<RE::TESPackage*>(a_pkg));
		return vm->DispatchStaticCall(RE::BSFixedString("ActorUtil"), RE::BSFixedString(a_fn), args, none);
	}

	bool overrideSet{ false };

	void GiveHorsePackage(RE::Actor* a_horse)
	{
		if (!trip.package) return;
		if (!overrideSet) {
			overrideSet = CallActorUtil("AddPackageOverride", a_horse, trip.package, true);
			logger::info("trip: package override through PapyrusUtil {}", overrideSet ? "sent" : "NOT available (PapyrusUtil missing?) - using the game's created-package call");
		}
		if (!overrideSet) a_horse->PutCreatedPackage(trip.package, true, false, true);
		a_horse->EvaluatePackage(true, false);
	}

	void TakeHorsePackage(RE::Actor* a_horse)
	{
		auto dh = RE::TESDataHandler::GetSingleton();
		auto pkg = dh ? dh->LookupForm<RE::TESPackage>(0x8F0, PLUGIN_FILE) : nullptr;
		if (a_horse && pkg) CallActorUtil("RemovePackageOverride", a_horse, pkg, false);
		overrideSet = false;
	}

	// after loading: no horse keeps the travel package (a save made mid-trip)
	void ReleaseSavedHorses()
	{
		for (auto& h : RE::ProcessLists::GetSingleton()->highActorHandles) {
			auto a = h.get();
			if (a && a->IsAMount()) TakeHorsePackage(a.get());
		}
	}

	bool HorseRunsTrip(RE::Actor* a_horse)
	{
		auto proc = a_horse->GetActorRuntimeData().currentProcess;
		return proc && trip.package && proc->GetRunningPackage() == trip.package;
	}

	// both seats on the horse: hers in the saddle, yours behind it
	bool PlaceTrip(RE::Actor* a_driver, RE::Actor* a_horse, RE::PlayerCharacter* a_player)
	{
		auto root = a_horse->Get3D();
		if (!root || !a_driver->Get3D() || !a_player->Get3D()) return false;
		static RE::BSFixedString anchorName(g.anchorNode.c_str());
		RE::NiAVObject*          anchor = root->GetObjectByName(anchorName);
		const RE::NiPoint3       base = anchor ? anchor->world.translate : root->world.translate;
		trip.yaw = a_horse->data.angle.z;
		trip.driverPos = base + root->world.rotate * RE::NiPoint3{ 0.0f, -g.driverBack, g.driverUp };
		trip.playerPos = base + root->world.rotate * RE::NiPoint3{ g.side, -g.back, g.up };
		trip.have = true;
		using Seat_t = std::pair<RE::Actor*, RE::NiPoint3>;
		for (const auto& [a, pos] : { Seat_t{ a_driver, trip.driverPos }, Seat_t{ a_player, trip.playerPos } }) {
			if (a->GetCharController()) a->DetachCharController();
			a->data.location = pos;
			a->data.angle = RE::NiPoint3{ 0.0f, 0.0f, trip.yaw };
			a->Update3DPosition(true);
		}
		if (trip.frames++ % 600 == 0) {
			logger::info("trip to {}: horse ({:.0f},{:.0f},{:.0f}) {:.0f} units to go, package running={}", trip.where, a_horse->data.location.x,
				a_horse->data.location.y, a_horse->data.location.z, trip.target ? a_horse->GetPosition().GetDistance(trip.target.get()->GetPosition()) : -1.0f,
				HorseRunsTrip(a_horse));
		}
		return true;
	}

	void StartTrip(RE::Actor* a_driver, int a_dest)
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (trip.active) {
			Notify("You're already on your way.");
			return;
		}
		if (AnyRiding()) {
			Notify("Let your passengers down first.");
			return;
		}
		if (player->IsOnMount()) {
			Notify("Get off your horse first, then ask again.");
			return;
		}
		// read the handle straight from the player's data: calling QLastRiddenMount() through CommonLib's
		// cross-VR virtual wrapper passed the arguments in the wrong registers (by-value return) and crashed
		auto horse = player->GetInfoRuntimeData().lastRiddenMount.get();
		if (!horse || horse->IsDead() || !horse->Is3DLoaded() || horse->IsOnMount() || horse->GetDistance(player) > 3000.0f) {
			Notify("Your horse needs to be close by.");
			return;
		}
		auto target = DestinationRef(a_dest);
		if (!target) {
			Notify(DESTINATIONS[a_dest].marker ? "That place couldn't be found." : "Mark a place on your map first.");
			return;
		}
		if (target->IsDisabled()) {  // e.g. a Hearthfire home whose land you haven't bought yet
			Notify(std::format("{} isn't on your map yet.", DESTINATIONS[a_dest].name));
			return;
		}
		if (target->GetWorldspace() != horse->GetWorldspace() || !horse->GetWorldspace()) {
			Notify("You can't ride there from here.");
			return;
		}
		if (horse->GetPosition().GetDistance(target->GetPosition()) < g.arriveDistance) {
			Notify("You're already there.");
			return;
		}
		trip.package = MakeTravelPackage(target);
		if (!trip.package) {
			Notify("Ride Along couldn't make the travel package (see RideAlong.log).");
			return;
		}
		trip.active = true;
		trip.ending = trip.endQueued = false;
		trip.driver = a_driver->GetHandle();
		trip.horse = horse->GetHandle();
		trip.target = target->GetHandle();
		trip.where = a_dest == DEST_COUNT - 1 ? "the place on your map" : DESTINATIONS[a_dest].name;
		trip.start = trip.lastCheck = RE::GetDurationOfApplicationRunTime();
		trip.lastHorsePos = horse->GetPosition();
		trip.stuck = trip.nudges = trip.idleChecks = 0;
		overrideSet = false;
		trip.frames = 0;
		// her
		SetPassengerTag(a_driver, true, RANK_DRIVER);
		a_driver->StopCombat();
		SetCalm(a_driver, true);
		a_driver->DrawWeaponMagicHands(false);
		if (g.ghost) {
			SetGhost(a_driver, true);
			trip.ghostSet = true;
		}
		a_driver->DetachCharController();
		// you
		player->DrawWeaponMagicHands(false);
		RE::ControlMap::GetSingleton()->ToggleControls(TRIP_CONTROLS, false, false);
		trip.controlsOff = true;
		player->DetachCharController();
		PlaceTrip(a_driver, horse.get(), player);
		const bool okD = a_driver->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
		const bool okP = player->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
		BuildSyncList(horse.get());
		// the horse
		GiveHorsePackage(horse.get());
		logger::info("trip: {} takes you to {} (pose: her {}, you {})", a_driver->GetDisplayFullName(), trip.where, okD ? "ok" : "REJECTED", okP ? "ok" : "REJECTED");
		Notify(std::format("{} takes you to {}. Press your Ride Along key to stop.", a_driver->GetDisplayFullName(), trip.where));
	}

	// Ends the trip: the horse stops, both of you are set down beside it.
	void EndTrip(const char* a_why)
	{
		if (!trip.active || trip.ending) return;
		trip.ending = true;
		trip.have = false;
		auto player = RE::PlayerCharacter::GetSingleton();
		auto driver = trip.driver.get();
		auto horse = trip.horse.get();
		if (horse) {
			ResetTravelPackage();
			TakeHorsePackage(horse.get());
			horse->EvaluatePackage(true, true);  // the horse goes back to its own packages
		}
		if (driver) {
			SetCalm(driver.get(), false);
			if (trip.ghostSet) SetGhost(driver.get(), false);
			driver->NotifyAnimationGraph(RE::BSFixedString(g.exitEvent.c_str()));
		}
		player->NotifyAnimationGraph(RE::BSFixedString(g.exitEvent.c_str()));
		if (trip.controlsOff) {
			RE::ControlMap::GetSingleton()->ToggleControls(TRIP_CONTROLS, true, false);
			trip.controlsOff = false;
		}
		if (a_why && *a_why) Notify(a_why);
		logger::info("trip ended: {}", a_why ? a_why : "");
		const RE::ActorHandle dh = trip.driver, hh = trip.horse;
		trip.active = false;
		trip.package = nullptr;
		trip.driver.reset();
		trip.horse.reset();
		trip.target.reset();
		// set down a few frames later (ending the riding animation and rebuilding physics in one frame crashed FSMP)
		std::thread([dh, hh] {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			SKSE::GetTaskInterface()->AddTask([dh, hh] {
				auto               player = RE::PlayerCharacter::GetSingleton();
				auto               h = hh.get();
				RE::Actor*         anchor = h ? h.get() : player;
				const float        a = anchor->data.angle.z;
				const RE::NiPoint3 base = anchor->data.location;
				MoveTo(player, anchor, base + Rotate(a, { -110.0f, 0.0f, 0.0f }), RE::NiPoint3{ 0.0f, 0.0f, a });  // the horse's left
				if (auto d = dh.get(); d && !d->IsDead()) {
					MoveTo(d.get(), anchor, base + Rotate(a, { 110.0f, 0.0f, 0.0f }), RE::NiPoint3{ 0.0f, 0.0f, a });  // its right
				}
				std::thread([dh] {
					std::this_thread::sleep_for(std::chrono::milliseconds(300));
					SKSE::GetTaskInterface()->AddTask([] {
						auto p = RE::PlayerCharacter::GetSingleton();
						if (!p->GetCharController() && !trip.active) {
							p->InitHavok();  // MoveTo should have rebuilt it; if not, do it here
							p->MoveHavok(true);
							logger::info("trip: your physics rebuilt by hand ({})", p->GetCharController() ? "ok" : "STILL MISSING");
						}
					});
					std::this_thread::sleep_for(std::chrono::milliseconds(1700));
					SKSE::GetTaskInterface()->AddTask([dh] {
						if (auto d = dh.get(); d && !OnTrip(d.get())) {
							SetPassengerTag(d.get(), false);
							d->EvaluatePackage(true, false);
						}
					});
				}).detach();
			});
		}).detach();
	}

	// every frame while the trip runs (from Tick)
	void TickTrip(RE::PlayerCharacter* a_player)
	{
		if (!trip.active || trip.ending || trip.endQueued) return;
		auto driver = trip.driver.get();
		auto horse = trip.horse.get();
		auto target = trip.target.get();
		if (!driver || !horse || !target || driver->IsDead() || horse->IsDead() || a_player->IsDead()) {
			SKSE::GetTaskInterface()->AddTask([] { EndTrip("The ride is over."); });
			trip.endQueued = true;
			return;
		}
		if (horse->GetParentCell() && horse->GetParentCell()->IsInteriorCell()) {
			SKSE::GetTaskInterface()->AddTask([] { EndTrip(""); });
			trip.endQueued = true;
			return;
		}
		if (!PlaceTrip(driver.get(), horse.get(), a_player)) return;
		SyncGraphVars(horse.get(), driver.get());
		SyncGraphVars(horse.get(), a_player);
		// either of you knocked out of the riding pose (a cell change, her AI): put back
		for (RE::Actor* a : { driver.get(), static_cast<RE::Actor*>(a_player) }) {
			bool riding = true;
			if (a->GetGraphVariableBool(RE::BSFixedString("bIsRiding"), riding) && !riding) {
				a->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
			}
		}
		if (driver->IsInCombat()) driver->StopCombat();
		if (auto st = driver->AsActorState(); st && st->IsWeaponDrawn()) driver->DrawWeaponMagicHands(false);
		const auto now = RE::GetDurationOfApplicationRunTime();
		const float left = horse->GetPosition().GetDistance(target->GetPosition());
		if (left < g.arriveDistance) {
			const std::string text = std::format("You've arrived at {}.", trip.where);
			SKSE::GetTaskInterface()->AddTask([text] { EndTrip(text.c_str()); });
			trip.endQueued = true;
			return;
		}
		if (g.travelCombat == 1 && (horse->IsInCombat() || a_player->IsInCombat())) {
			SKSE::GetTaskInterface()->AddTask([] { EndTrip("Trouble! Everyone gets down."); });
			trip.endQueued = true;
			return;
		}
		if (g.travelCombat == 0 && horse->IsInCombat()) horse->StopCombat();
		// every 5 seconds: is the horse still on its way? Stuck horses get the package again, then a nudge, then the trip ends.
		if (now - trip.lastCheck >= 5000) {
			trip.lastCheck = now;
			if (!HorseRunsTrip(horse.get())) {
				if (++trip.idleChecks >= 4) {
					logger::warn("trip: the horse never started its travel package - giving up");
					SKSE::GetTaskInterface()->AddTask([] { EndTrip("The horse won't go. You get down (see RideAlong.log)."); });
					trip.endQueued = true;
					return;
				}
				GiveHorsePackage(horse.get());
				return;  // no stuck checks while it isn't even trying
			}
			trip.idleChecks = 0;
			const float moved = horse->GetPosition().GetDistance(trip.lastHorsePos);
			trip.lastHorsePos = horse->GetPosition();
			if (moved > 150.0f) {
				trip.stuck = 0;
			} else if (now - trip.start > 8000) {
				++trip.stuck;
				logger::info("trip: the horse moved {:.0f} units in 5 s (stuck x{})", moved, trip.stuck);
				if (trip.stuck == 1) {
					GiveHorsePackage(horse.get());
				} else if ((trip.stuck == 2 || trip.stuck == 3) && trip.nudges < 5) {
					++trip.nudges;
					// a small hop toward the destination, a little up so the horse drops onto the ground
					auto to = target->GetPosition() - horse->GetPosition();
					to.z = 0.0f;
					const float len = std::sqrt(to.x * to.x + to.y * to.y);
					if (len > 1.0f) {
						const RE::NiPoint3 spot = horse->GetPosition() + to * (250.0f / len) + RE::NiPoint3{ 0.0f, 0.0f, 80.0f };
						const RE::ActorHandle hh = trip.horse;
						SKSE::GetTaskInterface()->AddTask([hh, spot] {
							if (auto h = hh.get()) {
								MoveTo(h.get(), h.get(), spot, h->data.angle);
								GiveHorsePackage(h.get());
							}
						});
						logger::info("trip: nudged the horse 250 units toward {}", trip.where);
					}
				} else if (trip.stuck >= 4 || trip.nudges >= 5) {
					const std::string text = std::format("{} can't find a way through. You get down.", driver->GetDisplayFullName());
					SKSE::GetTaskInterface()->AddTask([text] { EndTrip(text.c_str()); });
					trip.endQueued = true;
				}
			}
		}
	}

	bool TripActive() { return trip.active; }

	void HandleTripChoice()
	{
		auto       actor = tripAskedH.get();
		const int  dest = tripAskedDest;
		const bool stop = tripAskedStop;
		tripAskedH.reset();
		tripAskedDest = -1;
		tripAskedStop = false;
		if (stop) {
			if (trip.active) EndTrip("You get down.");
			return;
		}
		if (actor && dest >= 0) StartTrip(actor.get(), dest);
	}

	// After loading nobody is on a trip: give you your controls back if a load cut one short.
	void LoadTripForms()
	{
		auto dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(PLUGIN_FILE)) return;
		travelGlobal = dh->LookupForm<RE::TESGlobal>(0x80C, PLUGIN_FILE);
		travelTopic = dh->LookupForm<RE::TESTopic>(0x814, PLUGIN_FILE);
		stopTopic = dh->LookupForm<RE::TESTopic>(0x815, PLUGIN_FILE);
		int found = 0;
		for (int i = 0; i < DEST_COUNT; ++i) {
			destTopic[i] = dh->LookupForm<RE::TESTopic>(0x820 + i, PLUGIN_FILE);
			found += destTopic[i] != nullptr;
		}
		int markers = 0;
		for (int i = 0; i < DEST_COUNT - 1; ++i) markers += DestinationRef(i) != nullptr;
		logger::info("{}: travel global {} topic {} stop {} destinations {}/{} map markers {}/{}", PLUGIN_FILE, travelGlobal != nullptr,
			travelTopic != nullptr, stopTopic != nullptr, found, DEST_COUNT, markers, DEST_COUNT - 1);
		if (travelGlobal) travelGlobal->value = g.travelOn ? 1.0f : 0.0f;
	}

	void ApplyTravelSetting()
	{
		if (travelGlobal) travelGlobal->value = g.travelOn ? 1.0f : 0.0f;
	}

	void ResetTrip()
	{
		ResetTravelPackage();
		if (trip.controlsOff) RE::ControlMap::GetSingleton()->ToggleControls(TRIP_CONTROLS, true, false);
		trip.active = trip.ending = trip.endQueued = trip.controlsOff = trip.ghostSet = false;
		trip.have = false;
		trip.package = nullptr;
		trip.driver.reset();
		trip.horse.reset();
		trip.target.reset();
		tripAskedH.reset();
		tripAskedDest = -1;
		tripAskedStop = false;
	}

	// ------------------------------------------------------------------ dialogue
	void BoardPending()
	{
		pendingQueued = false;
		auto                     player = RE::PlayerCharacter::GetSingleton();
		RE::NiPointer<RE::Actor> horse;
		auto                     list = std::move(pending);
		pending.clear();
		if (!player->IsOnMount() || !player->GetMount(horse) || !horse) return;
		for (auto& p : list) {
			auto follower = p.h.get();
			if (!follower || RiderOf(follower.get()) || follower->IsDead() || follower->IsDisabled() || follower->IsOnMount() || !IsHumanoid(follower.get())) {
				continue;
			}
			if (!follower->Is3DLoaded() || follower->GetDistance(player) > (g.range > 3000.0f ? g.range : 3000.0f)) {
				Notify(std::format("{} is too far away to climb on.", follower->GetDisplayFullName()));
				continue;
			}
			const int seat = FreeSeat(p.want);
			if (seat < 0) {
				Notify(std::format("There's no room for {} {}.", follower->GetDisplayFullName(), p.want == -1 ? "behind you" : SEAT_WHERE[p.want]));
				continue;
			}
			Board(follower.get(), horse.get(), seat);
		}
	}

	// Writes to the log why the Ride Along lines do or don't show for this NPC.
	void LogDialogueCheck()
	{
		auto                             mtm = RE::MenuTopicManager::GetSingleton();
		RE::NiPointer<RE::TESObjectREFR> ref;
		if (mtm) ref = mtm->speaker.get();
		auto npc = ref ? ref->As<RE::Actor>() : nullptr;
		if (!npc) return;
		auto        player = RE::PlayerCharacter::GetSingleton();
		std::string line = std::format("dialogue with {}: quest running={} global={} teammate={} currentFollower={} passenger={} playerMounted={} |",
			npc->GetDisplayFullName(), dialogueQuest && dialogueQuest->IsRunning(), dialogueGlobal ? dialogueGlobal->value : -1.0f,
			npc->IsPlayerTeammate(), currentFollowerFaction && npc->IsInFaction(currentFollowerFaction),
			passengerFaction && npc->IsInFaction(passengerFaction), player->IsOnMount());
		for (auto [topic, name] : { std::pair{ rideTopic, "Ride" }, std::pair{ offTopic, "Off" }, std::pair{ frontTopic, "Front" }, std::pair{ shouldersTopic, "Shoulders" } }) {
			if (!topic) {
				line += std::format(" {}: no topic", name);
				continue;
			}
			int pass = 0;
			for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
				auto info = topic->topicInfos[i];
				if (info && info->objConditions.IsTrue(npc, player)) ++pass;
			}
			line += std::format(" {}: {} of {} lines pass (offset {:X}, quest {:08X})", name, pass, topic->numTopicInfos, topic->firstFileOffset,
				topic->ownerQuest ? topic->ownerQuest->GetFormID() : 0);
			// each condition of the last line (the one without a voice condition) on its own
			if (topic->numTopicInfos > 0 && topic->topicInfos[topic->numTopicInfos - 1]) {
				std::string each;
				for (auto c = topic->topicInfos[topic->numTopicInfos - 1]->objConditions.head; c; c = c->next) {
					RE::ConditionCheckParams params(npc, player);
					each += std::format("f{}={} ", static_cast<int>(c->data.functionData.function.get()), c->IsTrue(params) ? 1 : 0);
				}
				line += " [" + each + "]";
			}
		}
		// for comparison: a vanilla follower topic (DialogueFollower)
		if (auto follow = RE::TESForm::LookupByID<RE::TESQuest>(0x000750BA)) {
			for (auto t : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESTopic>()) {
				if (t && t->ownerQuest == follow && t->numTopicInfos > 0) {
					line += std::format(" | vanilla follower topic {:08X}: {} lines", t->GetFormID(), t->numTopicInfos);
					break;
				}
			}
		}
		logger::info("{}", line);
	}

	void HandleDialogueChoice()
	{
		auto actor = askedH.get();
		const bool off = askedOff;
		const int  want = askedWant;
		asked = false;
		askedH.reset();
		if (!actor) return;
		if (off) {
			std::erase_if(pending, [&](const Pending& p) { return p.h.get().get() == actor.get(); });
			if (auto r = RiderOf(actor.get())) Alight(SeatOf(r), true);
			return;
		}
		if (RiderOf(actor.get())) {
			Notify(std::format("{} is already riding with you.", actor->GetDisplayFullName()));
			return;
		}
		auto                     player = RE::PlayerCharacter::GetSingleton();
		RE::NiPointer<RE::Actor> horse;
		if (player->IsOnMount() && player->GetMount(horse) && horse) {
			const int seat = FreeSeat(want);
			if (seat < 0) {
				Notify(std::format("There's no room {}.", want == -1 ? "behind you" : SEAT_WHERE[want]));
				return;
			}
			Board(actor.get(), horse.get(), seat);
		} else {
			std::erase_if(pending, [&](const Pending& p) { return p.h.get().get() == actor.get(); });
			pending.push_back({ actor->GetHandle(), want });
			Notify(std::format("{} will climb on {} when you mount.", actor->GetDisplayFullName(), want == -1 ? "behind you" : SEAT_WHERE[want]));
		}
	}

	// ------------------------------------------------------------------ every frame
	// runs every frame from the camera update: after the horse has moved, before the frame is drawn
	void Tick()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) return;
		if (!pending.empty() && !pendingQueued && player->IsOnMount()) {
			pendingQueued = true;
			SKSE::GetTaskInterface()->AddTask([] { BoardPending(); });
		}
		TickTrip(player);
		if (!AnyRiding()) return;
		auto       horse = horseH.get();
		const auto now = RE::GetDurationOfApplicationRunTime();
		if (!horse || horse->IsDead() || !player->IsOnMount()) {
			if (!alightQueued) {
				alightQueued = true;
				const bool horseGone = !horse || horse->IsDead();
				SKSE::GetTaskInterface()->AddTask([horseGone] {
					alightQueued = false;
					for (int s = 0; s < kSeatCount; ++s) {
						auto& r = riders[s];
						if (!r.h || r.finishing) continue;
						if (horseGone) {
							r.finishing = true;
							FinishAlight(s, false);
						} else {
							Alight(s, true);
						}
					}
				});
			}
			if (!horse || horse->IsDead()) return;  // nothing left to sit on: they're set down
		}
		// Outdoors a passenger is only moved along when her cell is about to unload or she is in another worldspace;
		// a plain cell border is fine.
		if (horse && now - cellMoveTime > 2000) {
			const auto hc = horse->GetParentCell();
			bool       moved = false;
			for (auto& r : riders) {
				auto p = r.h.get();
				if (!p) continue;
				const auto pc = p->GetParentCell();
				const bool needMove = hc && pc != hc &&
				                      (!pc || hc->IsInteriorCell() || pc->IsInteriorCell() || !pc->IsAttached() ||
				                          p->GetWorldspace() != horse->GetWorldspace() || !p->Is3DLoaded());
				if (!needMove) continue;
				moved = true;
				r.have = false;  // the old seat is in the old cell: let the move stand until she is placed again
				const RE::ActorHandle ph = r.h;
				const RE::ActorHandle hh = horseH;
				SKSE::GetTaskInterface()->AddTask([ph, hh] {
					auto p = ph.get();
					auto h = hh.get();
					auto r = RiderOf(p.get());
					if (!p || !h || !r) return;
					MoveTo(p.get(), h.get(), h->data.location, h->data.angle);
					r->posePending = true;
					r->bones.clear();
					r->bonesTo = nullptr;
					logger::info("cell change: passenger moved along");
				});
			}
			if (moved) {
				cellMoveTime = now;
				return;
			}
		}
		if (!horse) return;
		for (int s = 0; s < kSeatCount; ++s) {
			auto& r = riders[s];
			auto  passenger = r.h.get();
			if (!passenger || r.finishing) continue;
			if (passenger->IsDead()) {
				r.finishing = true;
				SKSE::GetTaskInterface()->AddTask([s] { FinishAlight(s, false); });
				continue;
			}
			if (!Place(r, passenger.get(), horse.get(), player)) continue;
			SyncGraphVars(player, passenger.get());
			if (r.posePending) {
				r.posePending = false;
				passenger->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
			}
			if (r.phase == Phase::kGettingOn) {
				if (now - r.phaseStart >= static_cast<std::uint32_t>(g.getOnSeconds * 1000.0f)) {
					r.phase = Phase::kSeated;
					SetPassengerTag(passenger.get(), true, SeatRank(r));
					passenger->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));  // start the seated clip from its first frame
					Place(r, passenger.get(), horse.get(), player);
				}
				continue;
			}
			if (r.phase == Phase::kGettingOff) {
				if (now - r.phaseStart >= static_cast<std::uint32_t>(g.getOffSeconds * 1000.0f)) {
					r.finishing = true;
					const bool announce = r.announce;
					SKSE::GetTaskInterface()->AddTask([s, announce] { FinishAlight(s, announce); });
				}
				continue;
			}
			// pose watchdog: a cell change or her AI can knock her out of the riding pose. Put her back.
			bool riding = true;
			if (passenger->GetGraphVariableBool(RE::BSFixedString("bIsRiding"), riding) && !riding) {
				const bool ok = passenger->NotifyAnimationGraph(RE::BSFixedString(g.poseEvent.c_str()));
				if (r.poseResends++ < 20) {
					std::int32_t st = -1;
					passenger->GetGraphVariableInt(RE::BSFixedString("iState"), st);
					logger::info("pose lost (iState={}) - resent '{}' {}", st, g.poseEvent, ok ? "accepted" : "REJECTED");
				}
			}
			if (g.noCombat) KeepPeaceful(r, passenger.get());
			if (auto st = passenger->AsActorState(); st && st->IsWeaponDrawn() && !MayDraw(passenger.get())) {
				passenger->DrawWeaponMagicHands(false);
			}
			if ((s == kBack || s == kBack2) && (r.princess ? g.princessCopyPose : g.copyPose)) {
				CopyPose(r, passenger.get(), player);
			}
			if (g.dumpGraphVars && !dumpedMoving && now - boardTime > 8000) {
				float speed = 0.0f;
				horse->GetGraphVariableFloat(RE::BSFixedString("Speed"), speed);
				if (speed > 150.0f) {
					dumpedMoving = true;
					DumpGraphVars(player, std::format("player, moving, horse Speed={:.0f}", speed).c_str());
					DumpGraphVars(passenger.get(), "passenger, moving");
				}
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

	// The passenger's own movement step (Actor::ApplyMovementDelta, called from the actor update): while she rides her
	// AI / animation movement is not applied - she stays exactly on her seat. Same approach as RSE - Shoulder or Saddle.
	struct MovementHook
	{
		static void Thunk(RE::Actor* a_actor, float a_delta)
		{
			if (a_actor && trip.active && trip.have) {
				if (a_actor->IsPlayerRef()) {
					a_actor->data.location = trip.playerPos;
					a_actor->data.angle.z = trip.yaw;
					return;
				}
				if (trip.driver.get().get() == a_actor) {
					a_actor->data.location = trip.driverPos;
					a_actor->data.angle.z = trip.yaw;
					return;
				}
			}
			if (a_actor) {
				for (auto& r : riders) {
					if (r.have && r.h && r.h.get().get() == a_actor) {
						a_actor->data.location = r.seatPos;
						a_actor->data.angle.z = r.seatYaw;
						return;  // skip the engine's movement for the passenger
					}
				}
			}
			original(a_actor, a_delta);
		}

		static void Install()
		{
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(36359, 37350), REL::VariantOffset(0xF0, 0xFB, 0xF0) };
			if (*reinterpret_cast<const std::uint8_t*>(target.address()) != 0xE8) {
				logger::warn("Actor::ApplyMovementDelta call not found in this game version - passengers are placed every frame without it");
				return;
			}
			auto& trampoline = SKSE::GetTrampoline();
			original = trampoline.write_call<5>(target.address(), Thunk);
			logger::info("hooked Actor::ApplyMovementDelta");
		}

		static inline REL::Relocation<decltype(Thunk)> original;
	};

	// A riding passenger's weapon draw: blocked (her AI kept drawing and the ride kept putting it away - the
	// draw/sheathe loop). Allowed only when Peaceful passenger is off, and then only a bow or crossbow if you chose that.
	struct DrawHook
	{
		static void Thunk(RE::Actor* a_actor, bool a_draw)
		{
			if (a_draw && a_actor && !a_actor->IsPlayerRef() && !MayDraw(a_actor)) {
				return;
			}
			original(a_actor, a_draw);
		}

		static void Install()
		{
			if (REL::Module::IsVR()) {
				logger::info("VR: weapon-draw hook skipped (the ride puts weapons away every frame instead)");
				return;
			}
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_Character[0] };
			original = vtbl.write_vfunc(0xA6, Thunk);
			logger::info("hooked Character::DrawWeaponMagicHands");
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
			if (!a_event) return RE::BSEventNotifyControl::kContinue;
			auto ui = RE::UI::GetSingleton();
			if (ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->IsInFreeCameraMode()) {
				return RE::BSEventNotifyControl::kContinue;  // photo mode / free camera: the keys belong to the camera
			}
			for (auto e = *a_event; e; e = e->next) {
				if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;
				const auto button = e->AsButtonEvent();
				if (!button) continue;
				if (e->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
					if (g.hotkeyEnabled && button->IsDown() && button->GetIDCode() == g.hotkey) {
						SKSE::GetTaskInterface()->AddTask([] { Toggle(); });
					}
				} else if (e->GetDevice() == RE::INPUT_DEVICE::kGamepad && g.padButton != 0 && button->GetIDCode() == g.padButton) {
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
			if (!a_event || !a_event->speakerRef || a_event->type.get() != RE::TESTopicInfoEvent::TopicInfoEventType::kTopicBegin) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto info = RE::TESForm::LookupByID<RE::TESTopicInfo>(a_event->topicInfoFormID);
			auto actor = a_event->speakerRef->As<RE::Actor>();
			if (!info || !actor || !info->parentTopic) return RE::BSEventNotifyControl::kContinue;
			const auto t = info->parentTopic;
			if (t == rideTopic || t == frontTopic || t == shouldersTopic || t == offTopic) {
				askedH = actor->GetHandle();
				askedOff = t == offTopic;
				askedWant = t == frontTopic ? kFront : t == shouldersTopic ? kShoulders : -1;
				asked = true;
			}
			if (t == stopTopic) {
				tripAskedStop = true;
				asked = true;
			}
			for (int i = 0; i < DEST_COUNT; ++i) {
				if (t == destTopic[i]) {
					tripAskedH = actor->GetHandle();
					tripAskedDest = i;
					asked = true;
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->opening && std::string_view(a_event->menuName.c_str()) == RE::DialogueMenu::MENU_NAME) {
				SKSE::GetTaskInterface()->AddTask([] { LogDialogueCheck(); });
			}
			if (a_event && !a_event->opening && std::string_view(a_event->menuName.c_str()) == RE::DialogueMenu::MENU_NAME && asked) {
				SKSE::GetTaskInterface()->AddTask([] {
					HandleDialogueChoice();
					HandleTripChoice();
				});
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// ------------------------------------------------------------------ SKSE Menu Framework page (optional; SE/AE)
	namespace Menu
	{
		struct Choice
		{
			const char*   name;
			std::uint32_t code;
		};
		constexpr Choice KEYS[] = { { "K", 0x25 }, { "G", 0x22 }, { "V", 0x2F }, { "B", 0x30 }, { "H", 0x23 }, { "J", 0x24 }, { "L", 0x26 },
			{ "N", 0x31 }, { "M", 0x32 }, { "O", 0x18 }, { "P", 0x19 }, { "U", 0x16 }, { "Y", 0x15 }, { "T", 0x14 }, { "X", 0x2D },
			{ "C", 0x2E }, { "Z", 0x2C }, { "F1", 0x3B }, { "F2", 0x3C }, { "F3", 0x3D }, { "F4", 0x3E }, { "F5", 0x3F }, { "F6", 0x40 },
			{ "F7", 0x41 }, { "F8", 0x42 }, { "F9", 0x43 }, { "F10", 0x44 }, { "F11", 0x57 }, { "F12", 0x58 }, { "Numpad 0", 0x52 },
			{ "Numpad 1", 0x4F }, { "Numpad 2", 0x50 }, { "Numpad 3", 0x51 }, { "Numpad 4", 0x4B }, { "Numpad 5", 0x4C },
			{ "Numpad 6", 0x4D }, { "Numpad 7", 0x47 }, { "Numpad 8", 0x48 }, { "Numpad 9", 0x49 }, { "Insert", 0xD2 }, { "Home", 0xC7 },
			{ "Page Up", 0xC9 }, { "Delete", 0xD3 }, { "End", 0xCF }, { "Page Down", 0xD1 }, { "Left Alt", 0x38 }, { "Right Alt", 0xB8 },
			{ "Right Ctrl", 0x9D }, { "Caps Lock", 0x3A }, { "Backspace", 0x0E }, { "` (tilde)", 0x29 } };
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

		void Dialogue() { SKSE::GetTaskInterface()->AddTask([] { ApplyDialogueSetting(false); }); }  // game data: on the main thread

		void __stdcall Render()
		{
			ImGui::TextWrapped("%s", "Changes apply right away. Press Save to keep them for next time.");

			ImGui::SeparatorText("Getting on and off");
			ImGui::TextWrapped("%s", "The key seats the nearest follower in the next free seat. Once nobody else can get on, it lets everyone down.");
			dirty |= ImGui::Checkbox("Use the keyboard key", &g.hotkeyEnabled);
			dirty |= PickCombo("Keyboard key", KEYS, g.hotkey);
			dirty |= PickCombo("Controller button (hold)", PAD, g.padButton);
			dirty |= ImGui::SliderFloat("Hold time (seconds)", &g.padHold, 0.0f, 2.0f, "%.1f");
			if (ImGui::Checkbox("Ride Along lines in follower dialogue", &g.dialogueEnabled)) {
				dirty = true;
				Dialogue();
			}
			if (!passengerFaction) ImGui::TextWrapped("%s", "(RideAlong.esp isn't active: the dialogue needs it.)");
			dirty |= ImGui::SliderFloat("Follower range", &g.range, 300.0f, 4000.0f, "%.0f");
			if (ImGui::Button("Everyone get down now")) SKSE::GetTaskInterface()->AddTask([] { AlightAll(true); });

			ImGui::SeparatorText("Seats");
			ImGui::TextWrapped("%s", "Switch a seat off and nobody is seated there; its dialogue line goes away too.");
			const char* lines[kSeatCount] = { "\"Ride with me.\"", "\"Ride with me.\" (when the first seat is taken)", "\"Ride in front of me.\"", "\"Ride on my shoulders.\"" };
			for (int s = 0; s < kSeatCount; ++s) {
				if (ImGui::Checkbox(std::format("{} - {}", SEAT_NAME[s], lines[s]).c_str(), &g.seatOn[s])) {
					dirty = true;
					Dialogue();
				}
			}
			if (!SeatAnimsInstalled()) ImGui::TextWrapped("%s", "The seat animations (OpenAnimationReplacer\\RideAlongSeats) weren't found: front and shoulder passengers sit astride, and there are no climb animations.");
			if (ImGui::CollapsingHeader("Behind you")) {
				dirty |= ImGui::SliderFloat("Behind the saddle##b", &g.back, -20.0f, 80.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Height##b", &g.up, -40.0f, 40.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Left / right##b", &g.side, -40.0f, 40.0f, "%.0f");
			}
			if (ImGui::CollapsingHeader("Second seat behind you")) {
				dirty |= ImGui::SliderFloat("Behind the saddle##b2", &g.b2Back, 0.0f, 120.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Height##b2", &g.b2Up, -40.0f, 40.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Left / right##b2", &g.b2Side, -40.0f, 40.0f, "%.0f");
			}
			if (ImGui::CollapsingHeader("In front of you")) {
				dirty |= ImGui::SliderFloat("Ahead (-) / behind (+) the saddle##f", &g.fBack, -100.0f, 40.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Height##f", &g.fUp, -60.0f, 60.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Left / right##f", &g.fSide, -40.0f, 40.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Turn (degrees)##f", &g.fTurn, -180.0f, 180.0f, "%.0f");
			}
			if (ImGui::CollapsingHeader("On your shoulders")) {
				dirty |= ImGui::SliderFloat("Back (+) / forward (-)##s", &g.sBack, -40.0f, 40.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Height##s", &g.sUp, -80.0f, 80.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Left / right##s", &g.sSide, -40.0f, 40.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Turn (degrees)##s", &g.sTurn, -180.0f, 180.0f, "%.0f");
			}

			ImGui::SeparatorText("Climbing on and off (seats behind you)");
			dirty |= ImGui::Checkbox("Climb-on animation", &g.getOnAnim);
			dirty |= ImGui::SliderFloat("Climb-on time (seconds)", &g.getOnSeconds, 0.5f, 4.0f, "%.2f");
			dirty |= ImGui::Checkbox("Climb-off animation", &g.getOffAnim);
			dirty |= ImGui::SliderFloat("Climb-off time (seconds)", &g.getOffSeconds, 0.5f, 4.0f, "%.2f");
			if (ImGui::CollapsingHeader("Where the climb plays")) {
				dirty |= ImGui::SliderFloat("Back (+) / forward (-)##m", &g.mBack, -100.0f, 100.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Height##m", &g.mUp, -100.0f, 100.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Left / right##m", &g.mSide, -100.0f, 100.0f, "%.0f");
			}

			ImGui::SeparatorText("Passengers");
			dirty |= ImGui::Checkbox("Passengers stay out of fights", &g.noCombat);
			dirty |= ImGui::Checkbox("Attacks pass through passengers (no hitting them by accident)", &g.ghost);
			dirty |= ImGui::Checkbox("Talk to passengers while riding", &g.talkWhileRiding);
			if (!g.talkWhileRiding) ImGui::TextWrapped("%s", "Off: no prompt on passengers, so you can open gates and talk to people from the saddle. Use the key or \"Everyone get down now\" to let them off. Applies to the next passengers who get on.");
			const char* weaponModes[] = { "Kept sheathed", "Bows and crossbows only" };
			dirty |= ImGui::Combo("Passenger weapons", &g.weapons, weaponModes, 2);
			ImGui::TextWrapped("%s", "Bows only: melee weapons are put away for the ride (a bow from her pack is equipped if she has one) and given back when she gets off. Passengers only shoot when \"stay out of fights\" is off.");

			ImGui::SeparatorText("She rides, you ride behind her");
			ImGui::TextWrapped("%s", "Ask a follower \"Take me somewhere on my horse.\" on foot, near your horse, and pick a destination. She takes the saddle, you sit behind her, and the horse rides the roads there. Press your Ride Along key (or tell her \"Stop here.\") to get down on the way.");
			if (ImGui::Checkbox("\"Take me somewhere on my horse.\" in follower dialogue", &g.travelOn)) {
				dirty = true;
				SKSE::GetTaskInterface()->AddTask([] { ApplyTravelSetting(); });
			}
			const char* speeds[] = { "Walk", "Trot", "Run" };
			dirty |= ImGui::Combo("Horse speed", &g.travelSpeed, speeds, 3);
			const char* combat[] = { "Keep riding", "Stop and get down" };
			dirty |= ImGui::Combo("If you're attacked on the way", &g.travelCombat, combat, 2);
			dirty |= ImGui::SliderFloat("Arrival distance", &g.arriveDistance, 300.0f, 3000.0f, "%.0f");
			if (ImGui::CollapsingHeader("Her seat in the saddle")) {
				dirty |= ImGui::SliderFloat("Back (+) / forward (-)##d", &g.driverBack, -40.0f, 40.0f, "%.0f");
				dirty |= ImGui::SliderFloat("Height##d", &g.driverUp, -40.0f, 40.0f, "%.0f");
				ImGui::TextWrapped("%s", "Your seat behind her uses the \"Behind you\" seat settings above.");
			}
			if (trip.active) {
				ImGui::Text("On the way to %s.", trip.where.c_str());
				if (ImGui::Button("Stop and get down")) SKSE::GetTaskInterface()->AddTask([] { EndTrip("You get down."); });
			}

			ImGui::SeparatorText("Princess seat (female passengers behind you)");
			dirty |= ImGui::Checkbox("Use the princess seat", &g.princess);
			if (g.princess && !PrincessAnimsInstalled()) {
				ImGui::TextWrapped("%s", "The side-saddle animations aren't installed (see the Princess Seat readme). Until then she rides astride.");
			}
			dirty |= ImGui::SliderFloat("Princess: behind the saddle", &g.pBack, -20.0f, 80.0f, "%.0f");
			dirty |= ImGui::SliderFloat("Princess: height", &g.pUp, -40.0f, 40.0f, "%.0f");
			dirty |= ImGui::SliderFloat("Princess: left / right", &g.pSide, -40.0f, 40.0f, "%.0f");
			dirty |= ImGui::SliderFloat("Princess: turn (degrees)", &g.pTurn, -180.0f, 180.0f, "%.0f");
			dirty |= ImGui::Checkbox("Princess: copy your leg movement", &g.princessCopyPose);
			dirty |= ImGui::Checkbox("Princess: re-wear shoes after getting off (high heels fix)", &g.refreshShoes);

			ImGui::Separator();
			for (int s = 0; s < kSeatCount; ++s) {
				if (auto p = riders[s].h.get()) ImGui::Text("%s: %s%s", SEAT_NAME[s], p->GetDisplayFullName(), riders[s].princess ? " (princess seat)" : "");
			}
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
			if (REL::Module::IsVR()) {
				logger::info("VR: settings are in RideAlong.ini");
				return;
			}
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
	logger::info("Ride Along {} loading on Skyrim {}{}", SKSE::GetPluginVersion().string(), a_skse->RuntimeVersion().string(),
		REL::Module::IsVR() ? " (VR)" : "");

	RideAlong::LoadSettings();
	SKSE::AllocTrampoline(64);
	RideAlong::MovementHook::Install();
	RideAlong::DrawHook::Install();
	RideAlong::CameraHook<0>::Install(RE::VTABLE_HorseCameraState[0], "HorseCameraState");
	RideAlong::CameraHook<1>::Install(RE::VTABLE_FirstPersonState[0], "FirstPersonState");
	RideAlong::CameraHook<2>::Install(RE::VTABLE_ThirdPersonState[0], "ThirdPersonState");
	RideAlong::CameraHook<3>::Install(RE::VTABLE_FreeCameraState[0], "FreeCameraState");  // photo mode: passengers stay seated

	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			RideAlong::Menu::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(RideAlong::InputSink::Get());
			RideAlong::LoadForms();
			RideAlong::LoadTripForms();
			RideAlong::ApplyDialogueSetting(false);
			RideAlong::SeatAnimsInstalled();
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
			RideAlong::ResetAll();
			RideAlong::ResetTrip();
			RideAlong::pending.clear();
			RideAlong::askedH.reset();
			RideAlong::asked = false;
			if (a_msg->type == SKSE::MessagingInterface::kNewGame) {
				RideAlong::ApplyDialogueSetting(true);
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			RideAlong::ApplyDialogueSetting(true);
			RideAlong::ReleaseSavedPassengers();
			RideAlong::ReleaseSavedHorses();
			break;
		default:
			break;
		}
	});
	return true;
}
