#include <chrono>
#include <emscripten.h>
#include <emscripten/bind.h>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "entity/entity.hpp"
#include "game.hpp"
#include "geometry/polygon.hpp"
#include "geometry/vector_2d.hpp"
#include "message/message.hpp"
#include "message/message_bus.hpp"
#include "message/message_payload.hpp"
#include "system/system.hpp"
#include "window.hpp"

#include "standard/2d/box2d/box2d_body.hpp"
#include "standard/2d/box2d/box2d_physics_system.hpp"
#include "standard/2d/camera/camera.hpp"
#include "standard/2d/primitive/primitive_system.hpp"
#include "standard/2d/transform/transform.hpp"
#include "standard/window/window_system.hpp"

JamJar::Game* G_GameInstance = nullptr;

struct MathChallengeComponent {
    int expected_answer;
    std::string challenge_text;
    
    MathChallengeComponent(int answer, const std::string& text) 
        : expected_answer(answer), challenge_text(text) {}
};

struct EnemyTagComponent {};

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
        for (auto const& [id, entity] : this->entities) {
            auto* body = entity.Get<JamJar::Standard::_2D::Box2DBody>();
            auto* enemyTag = entity.Get<EnemyTagComponent>();
            auto* challenge = entity.Get<MathChallengeComponent>();
            
            if (body && enemyTag && challenge) {
                JamJar::Vector2D currentPos = body->GetPosition();
                JamJar::Vector2D direction(-currentPos.x, -currentPos.y);
                float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                
                if (length > 0.1f) {
                    direction.x /= length;
                    direction.y /= length;
                    float speed = 3.0f;
                    body->SetLinearVelocity(JamJar::Vector2D(direction.x * speed, direction.y * speed));
                } else {
                    body->SetLinearVelocity(JamJar::Vector2D(0.0f, 0.0f));
                }

                float pctX = (currentPos.x + 15.0f) / 30.0f;
                
                float pctY = 1.0f - ((currentPos.y + 1.2f + 8.5f) / 17.0f);

                unsigned int entityId = id; 
                const char* text = challenge->challenge_text.c_str();

                MAIN_THREAD_EM_ASM({
                    if (Module.updateMonsterUI) {
                        Module.updateMonsterUI($0, UTF8ToString($1), $2, $3);
                    }
                }, entityId, text, pctX, pctY);
            }
        }
    }
};


class MathDuelGame : public JamJar::Game {
public:
    MathDuelGame(JamJar::MessageBus* messageBus) : JamJar::Game(messageBus) {}

    void OnStart() override {
        std::cout << "C++: Запуск игровых систем и сцены!" << std::endl;

        new JamJar::Standard::_2D::PrimitiveSystem(this->messageBus);
        new JamJar::Standard::_2D::Box2DPhysicsSystem(this->messageBus, JamJar::Vector2D(0.0f, 0.0f));
        
        // Регистрируем наш ИИ монстров
        new EnemyAISystem(this->messageBus);

        auto cameraEntity = new JamJar::Entity(this->messageBus);
        cameraEntity->Add(new JamJar::Standard::_2D::Transform(JamJar::Vector2D(0, 0), JamJar::Vector2D(1, 1)));
        cameraEntity->Add(new JamJar::Standard::_2D::Camera(JamJar::Color(0.1f, 0.1f, 0.1f, 1.0f), JamJar::Vector2D(30, 17)));

        auto player = new JamJar::Entity(this->messageBus);
        player->Add(new JamJar::Standard::_2D::Transform(JamJar::Vector2D(0, 0), JamJar::Vector2D(2, 2)));
        player->Add(new JamJar::Standard::_2D::Primitive(
            JamJar::Polygon({-0.5, 0.5,  0.5, 0.5,  0.5, -0.5,  -0.5, -0.5}), 
            JamJar::Material(JamJar::Color(0, 0, 1, 1))
        ));
        player->Add(new JamJar::Standard::_2D::Box2DBody(
            JamJar::Polygon({-0.5, 0.5,  0.5, 0.5,  0.5, -0.5,  -0.5, -0.5}),
            JamJar::Standard::_2D::Box2DBodyProperties({.type = b2_staticBody})
        ));

        SpawnEnemyFromDarkness(-20.0f, 0.0f);
        SpawnEnemyFromDarkness(20.0f, 0.0f); 
    }

private:
    void SpawnEnemyFromDarkness(float x, float y) {
        auto enemy = new JamJar::Entity(this->messageBus);
        enemy->Add(new JamJar::Standard::_2D::Transform(JamJar::Vector2D(x, y), JamJar::Vector2D(1.5, 1.5)));
        
        enemy->Add(new JamJar::Standard::_2D::Primitive(
            JamJar::Polygon({0, 0.5,  0.5, -0.5,  -0.5, -0.5}), 
            JamJar::Material(JamJar::Color(1, 0, 0, 1))
        ));
        
        enemy->Add(new JamJar::Standard::_2D::Box2DBody(
            JamJar::Polygon({0, 0.5,  0.5, -0.5,  -0.5, -0.5}),
            JamJar::Standard::_2D::Box2DBodyProperties({.density = 1.0f, .type = b2_dynamicBody})
        ));

        enemy->Add(new EnemyTagComponent());

        auto challenge = GenerateMathChallenge();
        enemy->Add(new MathChallengeComponent(challenge.expected_answer, challenge.challenge_text));
        
        std::cout << "Монстр вышел из темноты (" << x << ", " << y << "). Пример: " 
                  << challenge.challenge_text << " | Ответ: " << challenge.expected_answer << std::endl;
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

    auto* messageBus = new JamJar::MessageBus();

    new JamJar::Standard::WindowSystem(messageBus, window, "canvas-wrapper");

    G_GameInstance = new MathDuelGame(messageBus);

    return 0;
}

void StartGameSession() {
    if (G_GameInstance != nullptr) {
        G_GameInstance->Start();
    }
}

EMSCRIPTEN_BINDINGS(game_core_module) {
    emscripten::function("StartGameSession", &StartGameSession);
}
