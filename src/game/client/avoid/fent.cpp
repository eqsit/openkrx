#include "fent.h"

#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/krx/msvc_sort.h>
#include <game/client/krx/replay.h>
#include <game/client/prediction/entities/character.h>
#include <game/collision.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>

namespace OpenKrx
{
	namespace
	{
		float Unit(std::mt19937 &Random) { return (float)(Random() >> 8) * 0x1p-24f; }
		bool Bernoulli(std::mt19937 &Random)
		{
			const uint32_t A = Random(), B = Random();
			return (double)((uint64_t)(A >> 11) + (uint64_t)B * 0x200000) * 0x1p-53 < .5;
		}
		void Aim(CNetObj_PlayerInput &Input, float Angle, float Radius)
		{
			const vec2 Value = direction(Angle);
			const float Length = length(Value);
			Input.m_TargetX = (int)(Value.x / Length * Radius);
			Input.m_TargetY = (int)(Value.y / Length * Radius);
		}
		void Tick(CGameWorld &World, int Id, const CNetObj_PlayerInput &Input)
		{
			if(auto *pCharacter = World.GetCharacterById(Id))
				pCharacter->OnDirectInput(&Input);
			++World.m_GameTick;
			if(auto *pCharacter = World.GetCharacterById(Id))
				pCharacter->OnPredictedInput(&Input);
			World.Tick();
			World.m_PredictedEvents.clear();
		}
	}

	uint32_t CFent::Uniform(std::mt19937 &Random, uint32_t Count)
	{
		if(!Count)
			return 0;
		uint64_t Product = (uint64_t)Random() * Count;
		while((uint32_t)Product < (uint32_t)(-Count) % Count)
			Product = (uint64_t)Random() * Count;
		return (uint32_t)(Product >> 32);
	}

	std::vector<CFent::CCandidate> CFent::InitialPopulation(std::mt19937 &Random, int Count, int Ticks, const CNetObj_PlayerInput &Base, float Radius)
	{
		std::vector<CCandidate> vPopulation;
		vPopulation.reserve(Count);
		for(int I = 0; I < Count; ++I)
		{
			CCandidate Candidate;
			bool Hook = Random() >> 31;
			int Direction = (int)Uniform(Random, 3) - 1;
			bool Jump = Random() >> 31;
			int JumpValue = Jump;
			for(int T = 0; T < Ticks;)
			{
				auto Input = Base;
				if(Direction == 0)
					Direction = (int)Uniform(Random, 3) - 1;
				Input.m_Direction = Direction;
				if(!Hook)
					Hook = Uniform(Random, 2) == 1;
				Input.m_Hook = Hook;
				if(!Jump)
				{
					Jump = Uniform(Random, 2) != 0;
					JumpValue = Jump;
					Input.m_Jump = JumpValue;
				}
				else
				{
					// Observed in both 2d8d10 pseudocode and instructions 2d8ede:
					// the sticky-jump branch writes Direction, not Jump.
					Input.m_Direction = JumpValue;
				}
				if((int32_t)Random() < 0)
					Input.m_Fire += 2;
				Aim(Input, Unit(Random) * 2 * pi, Radius);
				const int Repeats = Hook ? 5 : Input.m_Direction ? 3 :
										   0;
				for(int J = 0; J < Repeats; ++J)
					Candidate.m_vInputs.push_back(PackFentInput(Input));
				T += Repeats ? Repeats : 1;
			}
			vPopulation.push_back(std::move(Candidate));
		}
		return vPopulation;
	}

	void CFent::Mutate(std::mt19937 &Random, std::vector<CCandidate> &vPopulation, float Dosage, float Radius)
	{
		for(auto &Candidate : vPopulation)
			for(auto &Packed : Candidate.m_vInputs)
			{
				auto Input = UnpackFentInput(Packed);
				if(Unit(Random) < Dosage * .7f)
					Input.m_Direction = (int)Uniform(Random, 3) - 1;
				if(Unit(Random) < Dosage * .45f)
					Input.m_Hook = Bernoulli(Random);
				if(Unit(Random) < Dosage * .7f)
					Input.m_Jump = Bernoulli(Random);
				if(Unit(Random) < Dosage * .7f)
					Input.m_Fire += 2;
				if(Unit(Random) < Dosage * .7f)
					Aim(Input, Unit(Random) * 2 * pi, Radius);
				Packed = PackFentInput(Input);
			}
	}

	float CFent::Evaluate(const CCandidate &Candidate, const CGameWorld &Source, int Id, const CFlowField &Field, bool LightTiles)
	{
		CGameWorld World;
		World.CopyWorldIsolated(&Source);
		auto *pCharacter = World.GetCharacterById(Id);
		if(!pCharacter || Candidate.m_vInputs.empty())
			return FLT_MAX;
		vec2 Origin = pCharacter->Core()->m_Pos;
		float Speeds = length(pCharacter->Core()->m_Vel), Score = 0;
		CNetObj_PlayerInput Input{};
		for(uint32_t Packed : Candidate.m_vInputs)
		{
			Input = UnpackFentInput(Packed);
			Tick(World, Id, Input);
			pCharacter = World.GetCharacterById(Id);
			if(!pCharacter)
				return FLT_MAX;
			const auto *pCell = Field.At(pCharacter->Core()->m_Pos);
			if(!pCell || !pCell->Reachable())
				return FLT_MAX;
			if(!LightTiles && pCharacter->m_FreezeTime > 0)
				return FLT_MAX;
			const bool Frozen = pCharacter->m_FreezeTime > 0;
			if(!Frozen)
			{
				const float Alignment = dot(pCharacter->Core()->m_Vel, pCell->m_Flow);
				if(Alignment > 0)
					return FLT_MAX;
				Score += Alignment * 1000.0f;
			}
			const int Index = World.Collision()->GetPureMapIndex(pCharacter->Core()->m_Pos);
			if(World.Collision()->GetTileIndex(Index) == TILE_FINISH || World.Collision()->GetFrontTileIndex(Index) == TILE_FINISH)
				return -1e9f;
			Speeds += length(pCharacter->Core()->m_Vel);
		}
		Score = Score - distance(pCharacter->Core()->m_Pos, Origin) - Speeds / Candidate.m_vInputs.size();
		Origin = pCharacter->Core()->m_Pos;
		Speeds = length(pCharacter->Core()->m_Vel);
		vec2 LastPosition = Origin;
		for(int I = 0; I < 12; ++I)
		{
			Tick(World, Id, Input);
			pCharacter = World.GetCharacterById(Id);
			if(!pCharacter)
				break;
			LastPosition = pCharacter->Core()->m_Pos;
			const auto *pCell = Field.At(pCharacter->Core()->m_Pos);
			if(!pCell || !pCell->Reachable() || (!LightTiles && pCharacter->m_FreezeTime > 0))
				break;
			const bool Frozen = pCharacter->m_FreezeTime > 0;
			if(!Frozen)
				Score += dot(pCharacter->Core()->m_Vel, pCell->m_Flow) * 1750.0f;
			Speeds += length(pCharacter->Core()->m_Vel);
		}
		return Score - distance(LastPosition, Origin) - Speeds / 11.0f;
	}

	CFent::~CFent() { Stop(); }

	bool CFent::Busy() const
	{
		return Running() || m_Done.load() || (g_Config.m_AvoidFreeze && g_Config.m_AvoidAgentType == 3);
	}

	void CFent::Stop()
	{
		m_Cancel.store(true);
		if(m_Worker.joinable())
			m_Worker.join();
		m_Running.store(false);
	}

	void CFent::OnReset()
	{
		Stop();
		m_Done.store(false);
		m_pInitial.reset();
		m_pCollision.reset();
		std::lock_guard Lock(m_Mutex);
		m_vInputs.clear();
		m_vPath.clear();
		m_PausedForScan = false;
	}

	void CFent::Search(CSettings Settings, CFlowField Field)
	{
		{
			CGameWorld World;
			World.CopyWorldIsolated(m_pInitial.get());
			std::mt19937 Random(std::random_device{}());
			std::vector<CNetObj_PlayerInput> vInputs;
			std::vector<CPathPoint> vPath;
			int FrozenTicks = 0;
			bool FinishPending = false;
			for(int SearchTick = 0; SearchTick <= Settings.m_Ticks && !m_Cancel.load(); ++SearchTick)
			{
				auto *pCharacter = World.GetCharacterById(m_Id);
				if(!pCharacter)
					break;
				CNetObj_PlayerInput Base{};
				Base.m_WantedWeapon = pCharacter->GetWeaponGot(WEAPON_LASER) ? WEAPON_LASER + 1 : pCharacter->GetWeaponGot(WEAPON_GRENADE) ? WEAPON_GRENADE + 1 :
													  pCharacter->GetWeaponGot(WEAPON_SHOTGUN)         ? WEAPON_SHOTGUN + 1 :
																			     0;
				auto vPopulation = InitialPopulation(Random, Settings.m_Actions, Settings.m_ActionTicks, Base, Settings.m_MouseRadius);
				float Mutation = 1;
				for(int Generation = 0; Generation < Settings.m_Generations && !m_Cancel.load(); ++Generation)
				{
					for(auto &Candidate : vPopulation)
					{
						if(m_Cancel.load())
							break;
						const float Score = Evaluate(Candidate, World, m_Id, Field, Settings.m_LightTiles);
						// 2d83c0 leaves the previous score untouched on invalid paths.
						if(Score != FLT_MAX)
							Candidate.m_Score = Score;
					}
					MsvcSort(vPopulation.begin(), vPopulation.end(), (ptrdiff_t)vPopulation.size(), [](const auto &A, const auto &B) { return A.m_Score < B.m_Score; });
					// Both 2d8aa0 (evaluation) and its caller 2dac60 sort.
					MsvcSort(vPopulation.begin(), vPopulation.end(), (ptrdiff_t)vPopulation.size(), [](const auto &A, const auto &B) { return A.m_Score < B.m_Score; });
					if(vPopulation.empty() || vPopulation.front().m_Score <= -1e9f)
						break;
					const int EliteCount = (int)(Settings.m_Actions * .2f);
					std::vector<CCandidate> vNext(vPopulation.begin(), vPopulation.begin() + std::min(EliteCount, (int)vPopulation.size()));
					const int Children = Settings.m_Actions - EliteCount;
					std::mt19937 TournamentRandom(std::random_device{}());
					std::vector<CCandidate> vParents;
					for(int I = 0; I < Children / 2; ++I)
					{
						CCandidate Best;
						Best.m_Score = FLT_MAX;
						for(int Draw = 0; Draw < 5; ++Draw)
						{
							const auto &Candidate = vPopulation[Uniform(TournamentRandom, vPopulation.size())];
							if(Candidate.m_Score < Best.m_Score)
								Best = Candidate;
						}
						vParents.push_back(std::move(Best));
					}
					if(vParents.empty())
						break;
					std::mt19937 CrossoverRandom(std::random_device{}());
					std::vector<CCandidate> vChildren;
					for(int I = 0; I < Children; ++I)
					{
						const auto &A = vParents[Uniform(CrossoverRandom, vParents.size())].m_vInputs;
						const auto &B = vParents[Uniform(CrossoverRandom, vParents.size())].m_vInputs;
						const float Choice = Unit(CrossoverRandom);
						const size_t Size = std::min(A.size(), B.size());
						CCandidate Child;
						if(Choice < .4f)
						{
							Child.m_vInputs.insert(Child.m_vInputs.end(), A.begin(), A.begin() + Size / 2);
							Child.m_vInputs.insert(Child.m_vInputs.end(), B.begin() + Size / 2, B.end());
						}
						else
							for(size_t J = 0; J < Size; ++J)
								if(Choice < .6f)
									Child.m_vInputs.push_back(CrossoverRandom() & 1 ? B[J] : A[J]);
								else
								{
									auto Input = UnpackFentInput(A[J]);
									const auto Other = UnpackFentInput(B[J]);
									Input.m_TargetX = (Input.m_TargetX + Other.m_TargetX) / 2;
									Input.m_TargetY = (Input.m_TargetY + Other.m_TargetY) / 2;
									Child.m_vInputs.push_back(PackFentInput(Input));
								}
						vChildren.push_back(std::move(Child));
					}
					Mutate(Random, vChildren, Mutation, Settings.m_MouseRadius);
					vNext.insert(vNext.end(), std::make_move_iterator(vChildren.begin()), std::make_move_iterator(vChildren.end()));
					vPopulation = std::move(vNext);
					Mutation = std::max(.1f, Mutation * .96f);
				}
				if(vPopulation.empty() || m_Cancel.load())
					break;
				// GoodCrack consumes the first final-generation member without an
				// extra evaluation after the last crossover/mutation pass.
				const auto &Best = vPopulation.front();
				FrozenTicks = 0;
				for(uint32_t Packed : Best.m_vInputs)
				{
					if(m_Cancel.load())
						break;
					const auto Input = UnpackFentInput(Packed);
					vInputs.push_back(Input);
					Tick(World, m_Id, Input);
					pCharacter = World.GetCharacterById(m_Id);
					if(!pCharacter)
						break;
					vPath.push_back({pCharacter->Core()->m_Pos, (pCharacter->Core()->m_TriggeredEvents & COREEVENT_GROUND_JUMP) != 0});
					if(pCharacter->m_FreezeTime > 0)
						++FrozenTicks;
					const vec2 Tile((float)(int)(pCharacter->Core()->m_Pos.x / 32.0f), (float)(int)(pCharacter->Core()->m_Pos.y / 32.0f));
					if((!Settings.m_LightTiles && pCharacter->m_FreezeTime != 0) || (Settings.m_LightTiles && FrozenTicks >= 250) || FinishPending || distance(Tile, Field.Goal() / 32.0f) <= 5)
						goto Completed;
					if(Best.m_Score == -1e9f)
						FinishPending = true;
				}
				{
					std::lock_guard Lock(m_Mutex);
					m_vPath = vPath;
				}
			}
		Completed:
			{
				std::lock_guard Lock(m_Mutex);
				m_vInputs = std::move(vInputs);
				m_vPath = std::move(vPath);
			}
		}
		m_Done.store(true);
		m_Running.store(false);
	}

	void CFent::Update()
	{
		if(m_Done.exchange(false))
		{
			if(m_Worker.joinable())
				m_Worker.join();
			if(m_pInitial && !m_Failed && !m_Cancel.load())
			{
				m_pInitial->RebindCollision(Collision());
				GameClient()->m_Tas.ImportInputs(*m_pInitial, m_Id, m_vInputs);
			}
			g_Config.m_AvoidFreeze = 0;
		}
		const bool Enabled = g_Config.m_AvoidFreeze && g_Config.m_AvoidAgentType == 3;
		if(!Enabled || Client()->State() != IClient::STATE_ONLINE)
		{
			if(Running())
				Stop();
			return;
		}
		if(Running())
			return;
		GameClient()->m_Tas.UpdatePathFinding();
		const auto &Field = GameClient()->m_Tas.FlowField();
		if(!Field.Ready())
			return;
		m_Id = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
		const CGameWorld *pSource = GameClient()->m_Tas.Active() ? GameClient()->m_Tas.World() : &GameClient()->KrxPredictedWorld();
		if(!pSource || !const_cast<CGameWorld *>(pSource)->GetCharacterById(m_Id))
			return;
		if(m_Worker.joinable())
			m_Worker.join();
		m_pCollision = Collision()->CreateIsolatedCopy();
		m_pInitial = std::make_unique<CGameWorld>();
		m_pInitial->CopyWorldIsolated(pSource);
		m_pInitial->RebindCollision(m_pCollision.get());
		m_pInitial->m_WorldConfig.m_KrxPrediction = true;
		m_pInitial->m_WorldConfig.m_KrxExplosionStrengthValid = true;
		m_pInitial->m_WorldConfig.m_KrxExplosionStrength = GameClient()->m_aTuning[g_Config.m_ClDummy].m_ExplosionStrength;
		const auto *pLocal = m_pInitial->GetCharacterById(m_Id);
		const float Radius = pLocal->Core()->m_Tuning.m_HookLength + 56.0f;
		for(int Id = 0; Id < LEGACY_MAX_CLIENTS; ++Id)
		{
			auto *pCharacter = m_pInitial->GetCharacterById(Id);
			if(pCharacter && Id != m_Id && !pCharacter->m_IsLocal &&
				(!g_Config.m_AvoidPlayerPrediction || distance(pCharacter->m_Pos, pLocal->m_Pos) > Radius || GameClient()->IsOtherTeam(Id) ||
					(!GameClient()->m_Snap.m_aCharacters[Id].m_Active && pCharacter->m_SnapTicks > 10)))
				delete pCharacter;
		}
		for(int Type : {CGameWorld::ENTTYPE_PROJECTILE, CGameWorld::ENTTYPE_LASER, CGameWorld::ENTTYPE_DRAGGER})
			while(auto *pEntity = m_pInitial->FindFirst(Type))
				delete pEntity;
		CSettings Settings;
		if(g_Config.m_KrxFentAdvancedSettings)
		{
			Settings.m_Ticks = g_Config.m_KrxFentTicks;
			Settings.m_Actions = g_Config.m_KrxFentTweakerActions;
			Settings.m_ActionTicks = g_Config.m_KrxFentTweakerTicks;
			Settings.m_Generations = g_Config.m_KrxFentTweakerDosage;
		}
		else
		{
			Settings.m_Actions = g_Config.m_KrxFentQualitySetting == 2 ? 1000 : g_Config.m_KrxFentQualitySetting == 1 ? 160 :
																    88;
			Settings.m_Generations = g_Config.m_KrxFentQualitySetting == 2 ? 300 : Settings.m_Actions;
			g_Config.m_KrxFentTicks = Settings.m_Ticks;
			g_Config.m_KrxFentTweakerActions = Settings.m_Actions;
			g_Config.m_KrxFentTweakerTicks = Settings.m_ActionTicks;
			g_Config.m_KrxFentTweakerDosage = Settings.m_Generations;
		}
		Settings.m_LightTiles = g_Config.m_KrxFentLightTile != 0;
		Settings.m_MouseRadius = GameClient()->m_Controls.GetMaxMouseDistance();
		m_Cancel.store(false);
		m_Failed = false;
		m_Running.store(true);
		{
			std::lock_guard Lock(m_Mutex);
			m_vPath.clear();
			m_vInputs.clear();
		}
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "FentBot", "FentBot: Injecting fent...");
		GameClient()->m_Tas.PrepareForFent();
		m_Worker = std::thread([this, Settings, Field] { Search(Settings, Field); });
	}

	void CFent::UpdateScanCamera()
	{
		std::lock_guard Lock(m_Mutex);
		if(!g_Config.m_KrxFentSpectateScan || m_vPath.empty())
		{
			if(m_PausedForScan && Client()->State() == IClient::STATE_ONLINE)
				GameClient()->m_Chat.SendChat(0, "/pause");
			m_PausedForScan = false;
			return;
		}
		if(!m_PausedForScan)
		{
			GameClient()->m_Chat.SendChat(0, "/pause");
			m_PausedForScan = true;
		}
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const vec2 Position = m_vPath.back().m_Position;
			if(Position != vec2(0, 0))
			{
				m_ScanPosition = mix(m_ScanPosition, Position, std::min(1.0f, Client()->RenderFrameTime() * 4.2999997f));
				GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy] = m_ScanPosition;
			}
		}
	}

	void CFent::OnRender()
	{
		if(g_Config.m_AvoidAgentType != 3 || !g_Config.m_AvoidDrawPath)
			return;
		std::lock_guard Lock(m_Mutex);
		if(m_vPath.empty())
			return;
		const auto OldScreen = Graphics()->GetScreen();
		const auto &Camera = GameClient()->m_Camera;
		Graphics()->MapScreen(Graphics()->MapScreenToWorld(Camera.m_Center.x, Camera.m_Center.y, 100, 100, 100, 0, 0, Graphics()->ScreenAspect(), Camera.m_Zoom));
		const auto Segment = [&](vec2 From, vec2 To) {
			const vec2 Delta = To - From;
			if(length(Delta) < 1)
				return;
			const vec2 Normal = vec2(Delta.y, -Delta.x) / length(Delta) * Camera.m_Zoom * .875f;
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0, 1, 1, 1);
			IGraphics::CFreeformItem Quad(To - Normal, To + Normal, From - Normal, From + Normal);
			Graphics()->QuadsDrawFreeform(&Quad, 1);
			Graphics()->QuadsEnd();
		};
		vec2 Previous = m_vPath.front().m_Position;
		for(const auto &Point : m_vPath)
		{
			Segment(Previous, Point.m_Position);
			Previous = Point.m_Position;
		}
		for(const auto &Point : m_vPath)
			if(Point.m_Event)
			{
				Graphics()->QuadsBegin();
				Graphics()->SetColor(0, 1, 0, .5f);
				Graphics()->DrawCircle(Point.m_Position.x, Point.m_Position.y, 5, 32);
				Graphics()->QuadsEnd();
			}
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[WEAPON_GRENADE]);
		Graphics()->QuadsBegin();
		Graphics()->QuadsSetRotation(Client()->LocalTime() * 4 * pi);
		Graphics()->SetColor(1, 1, 1, 1);
		IGraphics::CQuadItem Endpoint(Previous.x, Previous.y, 32, 32);
		Graphics()->QuadsDraw(&Endpoint, 1);
		Graphics()->QuadsEnd();
		Graphics()->QuadsSetRotation(0);
		Graphics()->MapScreen(OldScreen);
	}
}
