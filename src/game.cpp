#include "game.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/html5.h>
#include <stdio.h>
#endif

#include "entity/entity.hpp"
#include "entity/entity_manager.hpp"
#include "geometry/polygon.hpp"
#include "geometry/vector_2d.hpp"
#include "hash.hpp"
#include "message/message.hpp"
#include "message/message_bus.hpp"
#include "message/message_payload.hpp"
#include "system/system.hpp"
#include "window.hpp"

#include "standard/2d/box2d/box2d_body.hpp"
#include "standard/2d/box2d/box2d_physics_system.hpp"
#include "standard/2d/camera/camera.hpp"
#include "standard/2d/transform/transform.hpp"
#include "standard/window/window_system.hpp"

#include "render/color.hpp"
#include "standard/2d/primitive/primitive.hpp"
#include "standard/2d/primitive/primitive_system.hpp"
#include "standard/2d/webgl2/webgl2_system.hpp"

const float MICROSECOND_TO_SECOND_CONVERSION = 1000000;
constexpr std::chrono::microseconds FRAMETIME_CAP = std::chrono::microseconds(250000);

JamJar::Game::Game(JamJar::MessageBus *messageBus)
    : messageBus(messageBus), isRunning(false), m_accumulator(std::chrono::microseconds(0)),
      m_currentTime(std::chrono::high_resolution_clock::now()) {
    messageBus->Subscribe(this, JamJar::Game::MESSAGE_STOP_GAME);
}

void JamJar::Game::Start() {
    this->OnStart();
    this->isRunning = true;
    this->startLoop();
}

void JamJar::Game::stop() {
    this->OnStop();
    this->isRunning = false;
}

void JamJar::Game::OnMessage(JamJar::Message *message) {
    switch (message->type) {
    case JamJar::Game::MESSAGE_STOP_GAME: {
        this->stop();
        return;
    }
    }
}

bool JamJar::Game::Loop(std::chrono::high_resolution_clock::time_point timestamp) {
    if (!this->isRunning) {
        return false;
    }

    auto timeDifference = timestamp - this->m_currentTime;
    auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(timeDifference);
    if (frameTime > FRAMETIME_CAP) {
        frameTime = FRAMETIME_CAP;
    }

    auto timeStep = std::chrono::microseconds(TIME_STEP);
    this->m_currentTime = timestamp;
    this->m_accumulator += frameTime;

    while (this->m_accumulator >= timeStep) {
        this->messageBus->Publish(std::make_unique<JamJar::MessagePayload<float>>(
            JamJar::System::MESSAGE_UPDATE, float(TIME_STEP) / MICROSECOND_TO_SECOND_CONVERSION));
        this->messageBus->Dispatch();
        this->m_accumulator -= timeStep;
    }

    auto alpha = float(this->m_accumulator.count()) / float(TIME_STEP);

    this->messageBus->Publish(std::make_unique<JamJar::MessagePayload<float>>(JamJar::Game::MESSAGE_PRE_RENDER, alpha));
    this->messageBus->Dispatch();

    this->messageBus->Publish(std::make_unique<JamJar::MessagePayload<float>>(JamJar::Game::MESSAGE_RENDER, alpha));

    this->messageBus->Publish(
        std::make_unique<JamJar::MessagePayload<float>>(JamJar::Game::MESSAGE_POST_RENDER, alpha));
    this->messageBus->Dispatch();
    return true;
}

void JamJar::Game::OnStart() {}
void JamJar::Game::OnStop() {}

#ifdef __EMSCRIPTEN__
EM_BOOL loopWrapper(double timestamp, void *userData) {
    auto game = static_cast<JamJar::Game *>(userData);
    auto now = std::chrono::high_resolution_clock::now();
    if (game->Loop(now)) {
        emscripten_request_animation_frame(loopWrapper, game);
    }
    return EM_TRUE;
}

void JamJar::Game::startLoop() {
    this->m_currentTime = std::chrono::high_resolution_clock::now();
    loopWrapper(0, this);
}
#else
void JamJar::Game::startLoop() {
    this->m_currentTime = std::chrono::high_resolution_clock::now();
    bool running = true;
    while (running) {
        auto now = std::chrono::high_resolution_clock::now();
        running = this->Loop(now);
    }
}
#endif

JamJar::Game* G_GameInstance = nullptr;

struct ActiveEnemy {
    unsigned int id;
    JamJar::Standard::_2D::Box2DBody* body;
    std::string challenge_text;
    int expected_answer;
};

std::vector<ActiveEnemy> G_ActiveEnemies;

class EnemyAISystem : public JamJar::System {
public:
    EnemyAISystem(JamJar::MessageBus* messageBus) : JamJar::System(messageBus) {
        this->messageBus->Subscribe(this, JamJar::System::MESSAGE_UPDATE);
    }

    void OnMessage(JamJar::Message* message) override {
        JamJar::System::OnMessage(message);
        
        if (message->type == JamJar::System::MESSAGE_UPDATE) {
            auto* updateMsg = static_cast<JamJar::MessagePayload<float>*>(message);
            float deltaTime = updateMsg->payload;
            
            UpdateEnemyAndUI(deltaTime);
        }
    }

private:
    void UpdateEnemyAndUI(float deltaTime) {
        for (auto& enemy : G_ActiveEnemies) {
            if (!enemy.body) continue;

            JamJar::Vector2D currentPos = enemy.body->GetPosition();
            
            JamJar::Vector2D direction(-currentPos.x, -currentPos.y);
            float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            
            if (length > 0.5f) {
                direction.x /= length;
                direction.y /= length;
                float speed = 1.8f; 
                enemy.body->SetLinearVelocity(JamJar::Vector2D(direction.x * speed, direction.y * speed));
            } else {
                enemy.body->SetLinearVelocity(JamJar::Vector2D(0.0f, 0.0f));
            }

            float pctX = (currentPos.x + 15.0f) / 30.0f;
            float pctY = 1.0f - ((currentPos.y + 8.5f) / 17.0f);

            const char* text = enemy.challenge_text.c_str();

#ifdef __EMSCRIPTEN__
            MAIN_THREAD_EM_ASM({
                if (Module.updateMonsterUI) {
                    Module.updateMonsterUI($0, UTF8ToString($1), $2, $3);
                }
            }, enemy.id, text, pctX, pctY);
#endif
        }
    }
};

class MathDuelGame : public JamJar::Game {
public:
    MathDuelGame(JamJar::MessageBus* messageBus) : JamJar::Game(messageBus) {}

    void OnStart() override {
        std::cout << "C++: Наполнение сцены объектами..." << std::endl;

        auto cameraEntity = new JamJar::Entity(this->messageBus);
        cameraEntity->Add(new JamJar::Standard::_2D::Transform());
        cameraEntity->Add(new JamJar::Standard::_2D::Camera(JamJar::Color(0.08f, 0.08f, 0.1f, 1.0f)));

        auto player = new JamJar::Entity(this->messageBus);
        player->Add(new JamJar::Standard::_2D::Transform(JamJar::Vector2D(0, 0), JamJar::Vector2D(2, 2)));
        player->Add(new JamJar::Standard::_2D::Primitive(
            JamJar::Polygon({-0.5f, 0.5f,  0.5f, 0.5f,  0.5f, -0.5f,  -0.5f, -0.5f,  -0.5f, 0.5f}),
            JamJar::Material(JamJar::Color(0.2f, 0.5f, 1.0f, 1.0f))
        ));

        JamJar::Standard::_2D::Box2DBodyProperties playerProps;
        auto* playerBody = new JamJar::Standard::_2D::Box2DBody(
            JamJar::Polygon({-0.5f, 0.5f,  0.5f, 0.5f,  0.5f, -0.5f,  -0.5f, -0.5f}),
            playerProps
        );
        playerBody->SetPosition(JamJar::Vector2D(0.0f, 0.0f));
        player->Add(playerBody);

        SpawnEnemyFromDarkness(-13.0f, 1.5f);
        SpawnEnemyFromDarkness(13.0f, -1.5f); 
    }

private:
    void SpawnEnemyFromDarkness(float x, float y) {
        auto enemyEntity = new JamJar::Entity(this->messageBus);
        enemyEntity->Add(new JamJar::Standard::_2D::Transform(JamJar::Vector2D(x, y), JamJar::Vector2D(2, 2)));
        
        enemyEntity->Add(new JamJar::Standard::_2D::Primitive(
            JamJar::Polygon({0.0f, 0.5f,  0.5f, -0.5f,  -0.5f, -0.5f,  0.0f, 0.5f}),
            JamJar::Material(JamJar::Color(1.0f, 0.2f, 0.2f, 1.0f))
        ));

        JamJar::Standard::_2D::Box2DBodyProperties enemyProps;
        enemyProps.density = 1.0f;

        auto* enemyBody = new JamJar::Standard::_2D::Box2DBody(
            JamJar::Polygon({0.0f, 0.5f,  0.5f, -0.5f,  -0.5f, -0.5f}),
            enemyProps
        );
        
        enemyBody->SetPosition(JamJar::Vector2D(x, y));
        enemyEntity->Add(enemyBody);

        auto challenge = GenerateMathChallenge();
        
        ActiveEnemy enemyData;
        enemyData.id = enemyEntity->id;
        enemyData.body = enemyBody;
        enemyData.challenge_text = challenge.challenge_text;
        enemyData.expected_answer = challenge.expected_answer;
        
        G_ActiveEnemies.push_back(enemyData);

        std::cout << "Монстр вышел из темноты ID: " << enemyEntity->id << " на позицию (" << x << ", " << y << "). Пример: " 
                  << challenge.challenge_text << std::endl;
    }

    struct ChallengeData {
        int expected_answer;
        std::string challenge_text;
    };

    ChallengeData GenerateMathChallenge() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> actionDist(0, 1);
        std::uniform_int_distribution<> answerDist(1, 10);

        int answer = answerDist(gen);
        std::string text = "";

        if (actionDist(gen) == 0) {
            std::uniform_int_distribution<> aDist(0, answer);
            int a = aDist(gen);
            int b = answer - a;
            text = std::to_string(a) + " + " + std::to_string(b);
        } else {
            std::uniform_int_distribution<> bDist(0, 10);
            int b = bDist(gen);
            int a = answer + b;
            text = std::to_string(a) + " - " + std::to_string(b);
        }

        return ChallengeData{answer, text};
    }
};

int main(int argc, char *argv[]) {
    auto window = JamJar::GetWindow("Math Duel: Magic Caster", 1280, 720);
    auto context = JamJar::GetCanvasContext();

    std::cout << "C++: Инициализация базовых подсистем JamJar..." << std::endl;

    auto* messageBus = new JamJar::MessageBus();

    new JamJar::EntityManager(messageBus);

    G_GameInstance = new MathDuelGame(messageBus);

    new JamJar::Standard::_2D::WebGL2System(messageBus, window, context);
    new JamJar::Standard::_2D::PrimitiveSystem(messageBus);
    new JamJar::Standard::_2D::Box2DPhysicsSystem(messageBus, JamJar::Vector2D(0.0f, 0.0f));
    new JamJar::Standard::WindowSystem(messageBus, window, "canvas-wrapper");
    
    new EnemyAISystem(messageBus);

    return 0;
}

void StartGameSession() {
    if (G_GameInstance != nullptr) {
        std::cout << "C++: Старт игрового сеанса через JS триггер." << std::endl;
        G_GameInstance->Start();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(game_core_module) {
    emscripten::function("StartGameSession", &StartGameSession);
}
#endif
