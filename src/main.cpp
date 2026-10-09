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
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <string>
#include <vector>

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
		bool          dismountInCombat{ false };
		std::string   syncVars{ "Speed,Direction,TurnDelta,SpeedSampled,IsSprinting,bIsSprinting,iSyncSprintState,fSpeedMult,HorseSpeed,TurnDeltaDamped,SpeedDamped" };
		bool          dumpGraphVars{ false };
		bool          copyPose{ true };
		std::string   copyBones{ "Thigh,Calf,Foot,Toe,Pelvis" };
		std::string   skipBones{ "Head,Neck,Eye,Jaw,Tongue,Brow,Lip,Cheek,Mouth,Root,Weapon,Shield,Quiver,Sheath,Magic,Camera,Saddle,Hair" };
		std::string   moveBones{ "" };
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
				} else if (key == "DismountInCombat") {
					g.dismountInCombat = std::stoi(val) != 0;
				}
			} catch (...) {
				logger::warn("RideAlong.ini: bad value for {}: '{}'", key, val);
			}
		}
		logger::info("sync variables: {}", g.syncVars);
		logger::info("settings: hotkey=0x{:X} back={} up={} side={} range={} anchor={} pose={} exit={} combatDismount={}",
			g.hotkey, g.back, g.up, g.side, g.range, g.anchorNode, g.poseEvent, g.exitEvent, g.dismountInCombat);
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
	// drive-by: a passenger in combat shoots from the saddle if she can; a melee-only passenger holds on
	bool            swappedToBow{ false };
	RE::FormID      prevRight{ 0 };
	RE::FormID      prevLeft{ 0 };
	bool            toldHoldOn{ false };
	std::uint32_t   combatCheckTime{ 0 };

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
		toldHoldOn = false;
		passengerH.reset();
		horseH.reset();
		posePending = false;
		leaving = false;
		cellMoveTime = 0;
		poseResends = 0;
		bones.clear();
		bonesTo = nullptr;
		bonesFrom = nullptr;
	}

	// Papyrus MoveTo: moves a reference and rebuilds its physics (gives the passenger its character controller back)
	void MoveTo(RE::TESObjectREFR* a_ref, RE::TESObjectREFR* a_target, const RE::NiPoint3& a_pos, const RE::NiPoint3& a_rot)
	{
		using func_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
		static REL::Relocation<func_t> func{ RELOCATION_ID(56227, 56626) };
		const RE::ObjectRefHandle      handle = a_target->RE::TESObjectREFR::GetHandle();
		func(a_ref, handle, a_target->GetParentCell(), a_target->GetWorldspace(), a_pos, a_rot);
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
		const RE::NiPoint3 offset{ g.side, -g.back, g.up };
		const RE::NiPoint3 pos = base + root->world.rotate * offset;

		if (a_passenger->GetCharController()) {
			a_passenger->DetachCharController();  // no capsule: nothing to collide with the horse, no AI walking
		}
		a_passenger->data.location = pos;
		seatPos = pos;
		seatYaw = a_horse->data.angle.z;
		haveSeat = true;
		a_passenger->data.angle = RE::NiPoint3{ 0.0f, 0.0f, a_horse->data.angle.z };
		a_passenger->Update3DPosition(true);

		if (frames++ % 600 == 0) {
			logger::info("riding: anchor={} pos=({:.0f},{:.0f},{:.0f})", anchor ? "found" : "MISSING (using horse root)", pos.x, pos.y, pos.z);
		}
		return true;
	}

	void Board(RE::Actor* a_passenger, RE::Actor* a_horse)
	{
		passengerH = a_passenger->GetHandle();
		horseH = a_horse->GetHandle();
		leaving = false;
		frames = 0;
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

	// ------------------------------------------------------------------ drive-by
	bool IsRanged(RE::Actor* a_actor)
	{
		for (const bool left : { false, true }) {
			auto f = a_actor->GetEquippedObject(left);
			if (!f) {
				continue;
			}
			if (auto w = f->As<RE::TESObjectWEAP>(); w && (w->IsBow() || w->IsCrossbow())) {
				return true;
			}
			if (f->Is(RE::FormType::Spell)) {
				return true;  // a caster counts as ranged
			}
		}
		return false;
	}

	// the strongest bow or crossbow she carries that she also has ammunition for
	RE::TESObjectWEAP* BestBow(RE::Actor* a_actor)
	{
		bool arrows = false, bolts = false;
		auto ammo = a_actor->GetInventory([](RE::TESBoundObject& o) { return o.Is(RE::FormType::Ammo); });
		for (auto& [obj, data] : ammo) {
			if (data.first > 0) {
				if (auto am = obj->As<RE::TESAmmo>(); am && am->IsBolt()) {
					bolts = true;
				} else {
					arrows = true;
				}
			}
		}
		RE::TESObjectWEAP* best = nullptr;
		int                bestDmg = -1;
		auto               inv = a_actor->GetInventory([](RE::TESBoundObject& o) {
            auto w = o.As<RE::TESObjectWEAP>();
            return w && (w->IsBow() || w->IsCrossbow());
        });
		for (auto& [obj, data] : inv) {
			auto w = obj->As<RE::TESObjectWEAP>();
			if (!w || data.first <= 0 || (w->IsCrossbow() ? !bolts : !arrows)) {
				continue;
			}
			if (static_cast<int>(w->attackDamage) > bestDmg) {
				bestDmg = w->attackDamage;
				best = w;
			}
		}
		return best;
	}

	void CombatControl(RE::Actor* a_passenger)
	{
		if (!a_passenger->IsInCombat()) {
			return;
		}
		if (IsRanged(a_passenger)) {
			return;  // bow, crossbow or spells in hand: let her fight from the saddle
		}
		if (!swappedToBow) {
			if (auto bow = BestBow(a_passenger)) {
				auto r = a_passenger->GetEquippedObject(false);
				auto l = a_passenger->GetEquippedObject(true);
				prevRight = r ? r->GetFormID() : 0;
				prevLeft = l ? l->GetFormID() : 0;
				swappedToBow = true;
				RE::ActorEquipManager::GetSingleton()->EquipObject(a_passenger, bow);
				Notify(std::format("{} draws a {}.", a_passenger->GetDisplayFullName(), bow->GetName()));
				logger::info("drive-by: equipped {} (was right={:X} left={:X})", bow->GetName(), prevRight, prevLeft);
				return;
			}
		}
		// nothing to shoot with: she holds on instead of jumping into a melee from the horse's back
		a_passenger->StopCombat();
		if (!toldHoldOn) {
			toldHoldOn = true;
			Notify(std::format("{} has no bow - holding on.", a_passenger->GetDisplayFullName()));
		}
	}

	// back to the weapons she had before the ride
	void RestoreWeapons(RE::Actor* a_passenger)
	{
		if (!swappedToBow || !a_passenger) {
			swappedToBow = false;
			return;
		}
		swappedToBow = false;
		auto  mgr = RE::ActorEquipManager::GetSingleton();
		auto  dom = RE::BGSDefaultObjectManager::GetSingleton();
		auto* rightSlot = dom ? dom->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECT::kRightHandEquip) : nullptr;
		auto* leftSlot = dom ? dom->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECT::kLeftHandEquip) : nullptr;
		if (auto r = RE::TESForm::LookupByID(prevRight)) {
			if (r->Is(RE::FormType::Weapon)) {
				mgr->EquipObject(a_passenger, r->As<RE::TESBoundObject>(), nullptr, 1, rightSlot);
			} else if (auto sp = r->As<RE::SpellItem>()) {
				mgr->EquipSpell(a_passenger, sp, rightSlot);
			}
		}
		if (auto l = RE::TESForm::LookupByID(prevLeft)) {
			if (l->Is(RE::FormType::Weapon)) {
				mgr->EquipObject(a_passenger, l->As<RE::TESBoundObject>(), nullptr, 1, leftSlot);
			} else if (l->Is(RE::FormType::Armor)) {
				mgr->EquipObject(a_passenger, l->As<RE::TESBoundObject>());  // shield / torch
			} else if (auto sp = l->As<RE::SpellItem>()) {
				mgr->EquipSpell(a_passenger, sp, leftSlot);
			} else if (auto light = l->As<RE::TESObjectLIGH>()) {
				mgr->EquipObject(a_passenger, light);
			}
		}
		logger::info("drive-by: weapons restored");
	}

	void Alight(bool a_announce)
	{
		haveSeat = false;
		auto passenger = passengerH.get();
		auto horse = horseH.get();
		Reset();
		if (!passenger) {
			return;
		}
		RE::Actor*         anchor = horse ? horse.get() : RE::PlayerCharacter::GetSingleton();
		const float        a = anchor->data.angle.z;
		const RE::NiPoint3 spot = anchor->data.location + RE::NiPoint3{ std::cos(a) * 110.0f, -std::sin(a) * 110.0f, 0.0f };  // to the horse's right

		passenger->NotifyAnimationGraph(RE::BSFixedString(g.exitEvent.c_str()));
		MoveTo(passenger.get(), anchor, spot, RE::NiPoint3{ 0.0f, 0.0f, a });
		RestoreWeapons(passenger.get());
		passenger->EvaluatePackage(true, false);
		logger::info("alight: {}", passenger->GetDisplayFullName());
		if (a_announce) {
			Notify(std::format("{} gets off.", passenger->GetDisplayFullName()));
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

	// runs every frame from the camera update: after the horse has moved, before the frame is drawn
	void Tick()
	{
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
		if (!player->IsOnMount() || (g.dismountInCombat && player->IsInCombat())) {
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
		if (now - combatCheckTime >= 250) {
			combatCheckTime = now;
			CombatControl(passenger.get());
		}
		SyncGraphVars(player, passenger.get());
		if (g.copyPose) {
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
				if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
					continue;
				}
				const auto button = e->AsButtonEvent();
				if (button && button->IsDown() && button->GetIDCode() == g.hotkey) {
					SKSE::GetTaskInterface()->AddTask([] { Toggle(); });
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
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
		case SKSE::MessagingInterface::kDataLoaded:
			RE::BSInputDeviceManager::GetSingleton()->AddEventSink(RideAlong::InputSink::Get());
			logger::info("input listener registered");
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			RideAlong::Reset();
			RideAlong::swappedToBow = false;
			break;
		default:
			break;
		}
	});
	return true;
}
