// Trajectory Assist - Geode mod
//
// 1. Simulates a "ghost" PlayerObject forward from the real player's state to
//    draw where you'll go (grey = no click, green = click now).
// 2. When you press jump, simulates clicking now vs. clicking N ticks later.
//    If clicking now would die but a slightly later click survives, the click
//    is held back until that tick (and the release is shifted to match).

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GJGameLevel.hpp>
#include <vector>

using namespace geode::prelude;

// ---------------------------------------------------------------- state ----

namespace {
	PlayerObject* g_ghost = nullptr;
	CCDrawNode* g_draw = nullptr;

	bool g_simulating = false;   // true while the ghost is being stepped
	bool g_simDied = false;      // set by the destroyPlayer hook
	bool g_firing = false;       // true while we replay a delayed input ourselves

	float g_lastDt = 1.f / 240.f; // real per-step dt, captured from the real player

	// delayed input bookkeeping (all in physics ticks)
	int g_pressDelay = -1;       // ticks until the delayed press fires (-1 = none)
	int g_releaseDelay = -1;     // ticks until the delayed release fires (-1 = none)
	int g_heldTicks = 0;         // ticks since the original (undelayed) press
	bool g_userHolding = false;  // user currently has the button down

	// Safe mode: once the assist is used, nothing from this level session is saved
	// or submitted. Stays on until you leave the level (like Mega Hack's noclip taint).
	bool g_safeMode = false;

	bool setting(char const* key) { return Mod::get()->getSettingValue<bool>(key); }
	int  settingInt(char const* key) { return static_cast<int>(Mod::get()->getSettingValue<int64_t>(key)); }

	void resetAssistState() {
		g_pressDelay = g_releaseDelay = -1;
		g_heldTicks = 0;
		g_userHolding = false;
	}

	void engageSafeMode() {
		if (g_safeMode || !Mod::get()->getSettingValue<bool>("safe-mode")) return;
		g_safeMode = true;
		Notification::create("Safe Mode: progress won't be saved", NotificationIcon::Info)->show();
	}

	void destroyGhost() {
		if (g_ghost) { g_ghost->release(); g_ghost = nullptr; }
		g_draw = nullptr;
	}

	PlayerObject* ensureGhost(GJBaseGameLayer* gl) {
		if (!g_ghost) {
			g_ghost = PlayerObject::create(1, 1, gl, gl->m_objectLayer, false);
			if (g_ghost) g_ghost->retain();
		}
		return g_ghost;
	}

	// Steps a copy of player 1 forward. clickAt < 0 means never click.
	// Returns true if the ghost survived all `steps` ticks.
	bool simulate(GJBaseGameLayer* gl, int clickAt, int holdTicks, int steps, std::vector<CCPoint>* path) {
		auto* real = gl->m_player1;
		auto* ghost = ensureGhost(gl);
		if (!real || !ghost) return true;

		g_simulating = true;
		g_simDied = false;

		ghost->copyAttributes(real);
		ghost->setPosition(real->getPosition());
		ghost->m_yVelocity = real->m_yVelocity;
		ghost->m_isDead = false;

		if (path) { path->clear(); path->reserve(steps + 1); path->push_back(ghost->getPosition()); }

		for (int t = 0; t < steps; ++t) {
			if (clickAt >= 0 && t == clickAt) ghost->pushButton(PlayerButton::Jump);
			if (clickAt >= 0 && t == clickAt + holdTicks) ghost->releaseButton(PlayerButton::Jump);

			ghost->update(g_lastDt);
			gl->checkCollisions(ghost, g_lastDt, false);

			if (path) path->push_back(ghost->getPosition());
			if (g_simDied || ghost->m_isDead) break;
		}

		bool survived = !(g_simDied || ghost->m_isDead);
		g_simulating = false;
		return survived;
	}

	// 0 = clicking now is fine (or nothing helps), >0 = delay that survives
	int findSafeDelay(GJBaseGameLayer* gl) {
		int steps = settingInt("predict-ticks");
		int hold = settingInt("hold-ticks");
		int maxDelay = settingInt("max-delay");

		if (simulate(gl, 0, hold, steps, nullptr)) return 0; // click now survives
		for (int d = 1; d <= maxDelay; ++d) {
			if (simulate(gl, d, hold, steps, nullptr)) return d;
		}
		return 0; // nothing in range saves you; don't interfere
	}

	bool assistActive(PlayLayer* pl) {
		if (!setting("autocorrect")) return false;
		if (setting("practice-only") && !pl->m_isPracticeMode) return false;
		return true;
	}
}

// ----------------------------------------------------------- death hook ----

class $modify(TAPlayLayer, PlayLayer) {
	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		if (g_simulating) {
			// the ghost hit something: record it, don't kill the real player
			if (player == g_ghost) g_simDied = true;
			return;
		}
		PlayLayer::destroyPlayer(player, obj);
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		resetAssistState();
		// enabling autocorrect taints the session, same idea as enabling noclip
		if (assistActive(this)) engageSafeMode();
	}

	void onQuit() {
		destroyGhost();
		resetAssistState();
		g_safeMode = false;
		PlayLayer::onQuit();
	}

	void levelComplete() {
		if (!g_safeMode) return PlayLayer::levelComplete();
		// test mode makes the game skip saving stats and submitting the completion
		bool old = m_isTestMode;
		m_isTestMode = true;
		PlayLayer::levelComplete();
		m_isTestMode = old;
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (!setting("show-trajectory") || !m_player1 || m_player1->m_isDead) {
			if (g_draw) g_draw->clear();
			return;
		}

		if (!g_draw || g_draw->getParent() != m_objectLayer) {
			g_draw = CCDrawNode::create();
			m_objectLayer->addChild(g_draw, 1000);
		}
		g_draw->clear();

		int steps = settingInt("predict-ticks");
		int hold = settingInt("hold-ticks");

		std::vector<CCPoint> idle, clicked;
		simulate(this, -1, hold, steps, &idle);
		simulate(this, 0, hold, steps, &clicked);

		auto drawPath = [](std::vector<CCPoint> const& p, ccColor4F col) {
			for (size_t i = 1; i < p.size(); ++i) {
				g_draw->drawSegment(p[i - 1], p[i], 0.75f, col);
			}
		};
		drawPath(idle,    { 0.7f, 0.7f, 0.7f, 0.8f });
		drawPath(clicked, { 0.2f, 1.0f, 0.3f, 0.9f });
	}
};

// ------------------------------------------------------- input handling ----

class $modify(TAPlayerObject, PlayerObject) {
	bool pushButton(PlayerButton btn) {
		auto* pl = PlayLayer::get();
		if (g_simulating || g_firing || !pl || this != pl->m_player1 || btn != PlayerButton::Jump) {
			return PlayerObject::pushButton(btn);
		}

		g_userHolding = true;
		g_heldTicks = 0;

		if (!assistActive(pl)) return PlayerObject::pushButton(btn);

		int delay = findSafeDelay(pl);
		if (delay <= 0) return PlayerObject::pushButton(btn);

		// swallow the early click and replay it `delay` ticks from now
		engageSafeMode();
		g_pressDelay = delay;
		g_releaseDelay = -1;
		return true;
	}

	bool releaseButton(PlayerButton btn) {
		auto* pl = PlayLayer::get();
		if (g_simulating || g_firing || !pl || this != pl->m_player1 || btn != PlayerButton::Jump) {
			return PlayerObject::releaseButton(btn);
		}

		g_userHolding = false;

		if (g_pressDelay > 0) {
			// the delayed press hasn't happened yet: shift the release by the same amount
			g_releaseDelay = g_pressDelay + g_heldTicks;
			return true;
		}
		return PlayerObject::releaseButton(btn);
	}

	// runs once per physics sub-step for every player
	void update(float dt) {
		auto* pl = PlayLayer::get();
		if (!g_simulating && pl && this == pl->m_player1) {
			g_lastDt = dt > 0.f ? dt : g_lastDt;

			if (g_userHolding) g_heldTicks++;

			if (g_pressDelay > 0 && --g_pressDelay == 0) {
				g_pressDelay = -1;
				g_firing = true;
				PlayerObject::pushButton(PlayerButton::Jump);
				g_firing = false;
				// user already let go before the delay finished
				if (!g_userHolding && g_releaseDelay < 0) g_releaseDelay = 1;
			}
			if (g_releaseDelay > 0 && --g_releaseDelay == 0) {
				g_releaseDelay = -1;
				g_firing = true;
				PlayerObject::releaseButton(PlayerButton::Jump);
				g_firing = false;
			}
		}
		PlayerObject::update(dt);
	}
};

// ------------------------------------------------------------ safe mode ----

class $modify(TAGJGameLevel, GJGameLevel) {
	void savePercentage(int percent, bool practice, int clicks, int attempts, bool chk) {
		if (g_safeMode) return; // don't record best %, practice %, attempts, etc.
		GJGameLevel::savePercentage(percent, practice, clicks, attempts, chk);
	}
};
