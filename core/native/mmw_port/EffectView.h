#pragma once
#include "Particle.h"
#include "Rendering/Renderer.h"
#include "Rendering/Camera.h"
#include <map>
#include <set>

namespace MikuMikuWorld
{
	class ScoreContext;
	class Note;
}

namespace MikuMikuWorld::Effect
{
	enum EffectType
	{
		fx_lane_critical,
		fx_lane_critical_flick,
		fx_lane_default,

		fx_note_normal_gen,
		fx_note_normal_aura,

		fx_note_critical_normal_gen,
		fx_note_critical_normal_aura,

		fx_note_flick_gen,
		fx_note_flick_aura,

		fx_note_critical_flick_gen,
		fx_note_critical_flick_aura,

		fx_note_long_gen,
		fx_note_long_aura,

		fx_note_critical_long_gen,
		fx_note_critical_long_aura,

		fx_note_flick_flash,
		fx_note_critical_flick_flash,
		fx_note_long_hold_via_aura,
		fx_note_critical_long_hold_via_aura,
		fx_note_trace_aura,
		fx_note_critical_trace_aura,

		fx_note_hold_aura,
		fx_note_long_hold_gen,

		fx_note_critical_long_hold_gen_aura,
		fx_note_critical_long_hold_gen,

		fx_count,
	};

	constexpr const char* effectNames[] =
	{
		"fx_lane_critical",
		"fx_lane_critical_flick",
		"fx_lane_default",

		"fx_note_normal_gen",
		"fx_note_normal_aura",

		"fx_note_critical_normal_gen",
		"fx_note_critical_normal_aura",

		"fx_note_flick_gen",
		"fx_note_flick_aura",

		"fx_note_critical_flick_gen",
		"fx_note_critical_flick_aura",

		"fx_note_long_gen",
		"fx_note_long_aura",

		"fx_note_critical_long_gen",
		"fx_note_critical_long_aura",

		"fx_note_flick_flash",
		"fx_note_critical_flick_flash",
		"fx_note_long_hold_via_aura",
		"fx_note_critical_long_hold_via_aura",
		"fx_note_trace_aura",
		"fx_note_critical_trace_aura",

		"fx_note_hold_aura",
		"fx_note_long_hold_gen",

		"fx_note_critical_long_hold_gen_aura",
		"fx_note_critical_long_hold_gen",
	};

	struct ParticleController
	{
		int refID{};
		Range time{ -1 , -1 };
		Effect::Transform worldOffset{};
		Effect::EmitterInstance effectRoot{};
		bool active{ false };

		void play(const Note& note, float start, float end);
		void stop();
	};

	class EffectPool
	{
	public:
		std::vector<ParticleController> pool;
		
		inline ParticleController& getNext()
		{
			ParticleController& controller = pool[next];
			next = (next + 1) % count;

			return controller;
		}

		// `simple` 剪掉子发射器（只留根那一层），见 EffectView::setSimpleEffect。
		void setup(EffectType type, int count, bool simple);
		inline int getCount() const { return count; }
		inline int getNextIndex() const { return next; }

	private:
		EffectType type{};
		int count{};
		int next{};

		EmitterInstance createEmitterFromParticle(int particleId, bool simple);
	};

	class EffectView
	{
	public:
		void addNoteEffects(const Note& note, const ScoreContext& context, float time);
		void addEffect(EffectType effect, const Note& note, const ScoreContext& context, float time);
		void addAuraEffect(EffectType effect, const Note& note, const ScoreContext& context, float time);
		void addLaneEffect(EffectType effect, const Note& note, const ScoreContext& context, float time);

		void drawUnderNoteEffects(Renderer* renderer, float time);
		void updateEffects(const ScoreContext& context, const Camera& camera, float time);
		void drawEffects(Renderer* renderer, float time);
		void update(const ScoreContext& context);

		void reset();

		void init();
		inline bool isInitialized() const { return initialized; }

		// CppSekai: 弱化打击特效（设置 → 画面）。两层一起收：
		//   1. 粒子树只建根发射器 —— 一个 note 特效原本是「根 + Ripple + 几组飞散
		//      粒子 + 方形碎片」拼起来的，砍掉 children 就只剩那一层基础贴图；
		//   2. spawn 时再跳过整层的光效（见 isSuppressed）。
		// 粒子树是按开关在 init() 里建好的，所以改值必须重建，这里自己调 init()。
		void setSimpleEffect(bool simple)
		{
			if (simpleEffect == simple) {
				return;
			}
			simpleEffect = simple;
			if (initialized) {
				init();
			}
		}
		bool isSimpleEffect() const { return simpleEffect; }

		inline bool isNoteEffectPlayed(int noteId) const
		{
			return playedEffectsNoteIds.find(noteId) != playedEffectsNoteIds.end();
		}

	private:
		Texture* effectsTex{ nullptr };
		bool initialized{ false };
		bool simpleEffect{ false };
		std::map<EffectType, EffectPool> effectPools;
		std::set<int> playedEffectsNoteIds;

		// 弱化特效时被整层跳过的类型（见 setSimpleEffect）。
		bool isSuppressed(EffectType effect) const;

		void drawEffectsInternal(EmitterInstance& emitter, Renderer* renderer, float time);
		void drawUnderNoteEffectsInternal(EmitterInstance& emitter, Renderer* renderer, float time);
		void drawParticles(const std::vector<ParticleInstance>& particles, const Particle& ref, Renderer* renderer, float time);
	};
}