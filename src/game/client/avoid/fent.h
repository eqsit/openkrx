#ifndef GAME_CLIENT_AVOID_FENT_H
#define GAME_CLIENT_AVOID_FENT_H

#include <game/client/component.h>
#include <game/client/krx/flow_field.h>
#include <game/client/prediction/gameworld.h>

#include <array>
#include <atomic>
#include <cfloat>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

class CCollision;
namespace OpenKrx
{
	class CFent : public CComponent
	{
	public:
		struct CSettings
		{
			int m_Ticks = 10000;
			int m_Actions = 88;
			int m_ActionTicks = 8;
			int m_Generations = 88;
			bool m_LightTiles = false;
			float m_MouseRadius = 400;
		};
		struct CCandidate
		{
			std::vector<uint32_t> m_vInputs;
			float m_Score = 0;
		};
		// 2d8aa0 -> 2d76b0 evaluates disjoint contiguous ranges in parallel,
		// waits for every range, then sorts. Each worker owns its simulation.
		class CEvaluator
		{
		public:
			CEvaluator(const CGameWorld &Source, int Id, const CFlowField &Field, bool LightTiles, unsigned NumThreads);
			~CEvaluator();
			void Evaluate(std::vector<CCandidate> &vPopulation, const std::atomic<bool> &Cancel);

		private:
			struct CImpl;
			std::unique_ptr<CImpl> m_pImpl;
		};
		struct CPathPoint
		{
			vec2 m_Position;
			bool m_Event;
		};
		~CFent() override;
		int Sizeof() const override { return sizeof(*this); }
		void OnReset() override;
		void OnShutdown() override { Stop(); }
		void OnRender() override;
		void UpdateScanCamera();
		void Update();
		void Stop();
		bool Running() const { return m_Running.load(); }
		bool Busy() const;
		static uint32_t Uniform(std::mt19937 &Random, uint32_t Count);
		static std::vector<CCandidate> InitialPopulation(std::mt19937 &Random, int Count, int Ticks, const CNetObj_PlayerInput &Base, float Radius);
		static void Mutate(std::mt19937 &Random, std::vector<CCandidate> &vPopulation, float Dosage, float Radius);
		static float Evaluate(const CCandidate &Candidate, const CGameWorld &Source, int Id, const CFlowField &Field, bool LightTiles);

	private:
		std::thread m_Worker;
		std::atomic<bool> m_Running{false}, m_Cancel{false}, m_Done{false};
		std::mutex m_Mutex;
		std::vector<CPathPoint> m_vPath;
		std::vector<CNetObj_PlayerInput> m_vInputs;
		std::unique_ptr<CCollision> m_pCollision;
		std::unique_ptr<CGameWorld> m_pInitial;
		std::array<CTuningParams, 256> m_aSearchTuning;
		int m_Id = -1;
		bool m_PausedForScan = false;
		vec2 m_ScanPosition = vec2(0, 0);
		bool m_Failed = false;
		void Search(CSettings Settings, CFlowField Field);
	};
}
#endif
