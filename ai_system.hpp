#pragma once

#include <vector>
#include <cmath> 

#include "tiny_ecs_registry.hpp"
#include "common.hpp"
#include "render_system.hpp"
#include <random>
using namespace std;
#include <list>

enum class NodeState {True, False, Running};

// Struct that holds information as to whether there is an player nearby - we use this to pass in info from the AI system into the nodes
struct AIStatus {
	bool playerNearby = false;
	bool playerAttackable = false;
	bool shouldAttack = true;
	Motion* aiMotion = nullptr;
	Motion* playerMotion = nullptr;
	Entity* aiEntity = nullptr;
	Entity* playerEntity = nullptr;
	//random ints from 1-1000 used for random behaviors 
	int rand = 0;
	int rand2 = 0;
};

class Node {  // This class represents each node in the behaviour tree.
	public:
		Node* parent;
		virtual Node* run() = 0;
		NodeState state;
		void setParent(Node* p) {parent = p;}
		Node* getParent() {return parent;}

};

class CompositeNode : public Node {  //  This type of Node follows the Composite Pattern, containing a list of other Nodes.
	private:
		std::list<Node*> children;
	public:
		const std::list<Node*>& getChildren() const {return children;}
		void addChild (Node* child) {children.emplace_back(child);}
};

class Selector : public CompositeNode {
	public:
		Node* run()  {
			for (Node* child : getChildren()) {  // The generic Selector implementation
				Node* childNext = child->run();
				if (child->state==NodeState::True) { // If one child succeeds, the entire operation run() succeeds.  Failure only results if all children fail.
					this->state = NodeState::True;
					return this->parent;

				} else if (child->state==NodeState::Running) { //If running, return the child that's still running
					this->state = NodeState::Running;
					return childNext;
				}
			}
			this->state = NodeState::False;
			return this->parent;  // All children failed so the entire run() operation fails.
		}
};


class Sequence : public CompositeNode {
	public:
		Node* run()  {
			for (Node* child : getChildren()) {  // The generic Sequence implementation.
				Node* childNext = child->run();
				if (child->state==NodeState::False) { // If one child fails, then entire operation run() fails.  Success only results if all children succeed.
					this->state = NodeState::False;
					return this->parent;
				}
				if (child->state==NodeState::Running) {
					this->state = NodeState::Running;
					return childNext;
				}
			}
			this->state = NodeState::True;
			return this->parent;  // All children suceeded, so the entire run() operation succeeds.
		}
};

class LeafNode : public Node {
	protected: 
			AIStatus* status;
	public:
			AIStatus* getStatus() { return status;}
			void setStatus(AIStatus* newStatus) {status = newStatus;}
};

class Patrol : public LeafNode{
	public:
		Node* run() {
			if (status->playerNearby) {
				//If a player is nearby, fail and begin chasing
				this->state = NodeState::False;
				return this->parent;
			} else {
				//randomly generate a number from 1 - 100 then perform an action based on the result:
				// 1 - 80 does nothing, i.e. continue moving/waiting 
				// 81 - 90 makes the mob stop moving
				// 91 - 100 makes the mob start moving in a random direction

				if (status->rand >= 97 && status->rand <= 98) {
					status->aiMotion->velocity = vec2(0,0);
				}
				if (status->rand >= 99 && status->rand <= 100) {
					//make mobs move slower than their base speed when patrolling so they "run" at the player when spotting
					float xvel = ((status->rand % 10) - 4.5) * status->aiMotion->speed/9;
					float yvel = ((status->rand2 % 10) - 4.5) * status->aiMotion->speed/9;
					status->aiMotion->velocity = vec2(xvel,yvel);
				}
				this->state = NodeState::Running;
				return this;
			}
		}
};

class ChasePlayer : public LeafNode{
	public:
		Node* run()  {
			Motion* enemyMotion = status->aiMotion;
			Motion* playerMotion = status->playerMotion;

			//Case 1: Enemy has reached player, return true and move to attack node
			if (status->playerAttackable && status->shouldAttack) {
				this->state = NodeState::True;
				return this->parent;

			//Case 2: Enemy can see player but has not reached them, return running and keep chasing
			} else if (status->playerNearby && status->shouldAttack) {

				//move towards the player
				float ratio = enemyMotion->speed / sqrt(pow(playerMotion->position.x - enemyMotion->position.x, 2) + pow(playerMotion->position.y - enemyMotion->position.y, 2));
				enemyMotion->velocity = vec2(ratio * (playerMotion->position.x - enemyMotion->position.x), ratio * (playerMotion->position.y - enemyMotion->position.y));

				this->state = NodeState::Running;
				return this;
			//Case 3: Enemy has lost sight of player or should no longer be attacking, return to patrol loop
			} else { 
				//set velocity to half so enemies visibly stop "running" after losing the player
				enemyMotion->velocity = enemyMotion->velocity/vec2(2,2);
				this->state = NodeState::False;
				return this->parent;
			}
		}

};

class AttackPlayer : public LeafNode {
public:
	Node* run() {
		if (status->playerAttackable && status->shouldAttack) {
			if (!registry.enemyAttacks.has(*status->aiEntity)) registry.enemyAttacks.emplace(*status->aiEntity);
			this->state = NodeState::Running;
			return this;
		}
		else {
			if (registry.enemyAttacks.has(*status->aiEntity)) registry.enemyAttacks.remove(*status->aiEntity);
			this->state = NodeState::False;
			return this->parent;
		}
	}
};

//stalk node used for goblins - 
// checks if there's another hostile mob near the player
// return "true" to initiate next behavior, otherwise maintain distance from player and return running
class StalkPlayer : public LeafNode {
public:
	Node* run() {
		Player& player = registry.players.get(*status->playerEntity);
		float playerDist = sqrtf(pow((status->aiMotion->position.x-status->playerMotion->position.x), 2)  + pow((status->aiMotion->position.y - status->playerMotion->position.y), 2));
		HasAI& ai = registry.hasAIs.get(*status->aiEntity);

		Motion* enemyMotion = status->aiMotion;
		Motion* playerMotion = status->playerMotion;

		if (status->shouldAttack) {
			//if other enemy is nearby (and thus attacking) return true
			this->state = NodeState::True;
			return this->parent;	
		}
		// if player is no longer visible to the ai fail
		if (!status->playerNearby) {
			this->state = NodeState::False;
			return this->parent;
		}
		//player is visible, not vulnerable, but too far away - approach
		if (playerDist > ai.detectionRadius * 0.6) {
				//move towards the player
				float ratio = enemyMotion->speed / sqrt(pow(playerMotion->position.x - enemyMotion->position.x, 2) + pow(playerMotion->position.y - enemyMotion->position.y, 2));
				enemyMotion->velocity = vec2(ratio * (playerMotion->position.x - enemyMotion->position.x), ratio * (playerMotion->position.y - enemyMotion->position.y));

				this->state = NodeState::Running;
				return this;

		} else if (playerDist > ai.detectionRadius * 0.55 && playerDist <= ai.detectionRadius * 0.6) {
			//Keep same position but face player
				float ratio = 0.0001 / sqrt(pow(playerMotion->position.x - enemyMotion->position.x, 2) + pow(playerMotion->position.y - enemyMotion->position.y, 2));
				enemyMotion->velocity = vec2(ratio * (playerMotion->position.x - enemyMotion->position.x), ratio * (playerMotion->position.y - enemyMotion->position.y));
				this->state = NodeState::Running;
				return this;	

		} else if (playerDist <= ai.detectionRadius * 0.55) {
				//move away from the player
				float ratio = -enemyMotion->speed / sqrt(pow(playerMotion->position.x - enemyMotion->position.x, 2) + 
														  pow(playerMotion->position.y - enemyMotion->position.y, 2));
				enemyMotion->velocity = vec2(ratio * (playerMotion->position.x - enemyMotion->position.x), 
											  ratio * (playerMotion->position.y - enemyMotion->position.y));
				this->state = NodeState::Running;
				return this;
		}
		//should never return here
		printf("ERROR StalkPlayer defaulted!");
		this->state = NodeState::False;
		return this->parent;
	}
};

class AISystem {
private:
    Entity playerEntity;
    Node* skeletonCurrentNode;
    Node* goblinCurrentNode;
    Node* mushroomCurrentNode;
    std::default_random_engine rng;
    std::uniform_real_distribution<float> uniformDist; // number between 0..1

    // Helper functions for behavior tree construction
    Node* CreateSkeletonBehaviorTree();
    Node* CreateGoblinBehaviorTree();
    Node* CreateMushroomBehaviorTree();
    Selector* CreateRootNode();
    Patrol* CreatePatrolNode(Node* parent);
    Sequence* CreateChaseSequenceNode(Node* parent, std::initializer_list<Node*> children);
    Sequence* CreateSequenceNode(Node* parent);
    StalkPlayer* CreateStalkPlayerNode(Node* parent);

public:
    // One ai status for all entities - continually updated
    AIStatus* status;

    AISystem();
    void Step(float elapsedMs, RenderSystem* renderer);
    void HandleEnemyAttacks(RenderSystem* renderer);
    void AttackPlayer(Entity damagingEnemy, float damage, RenderSystem* renderer);
    bool IsNearby(Motion& motion1, Motion& motion2, float nearbyRadius);
    void MoveBoid(Entity& entity);
    vec2 GroupBoid(Entity& entity);
    vec2 SeparateBoid(Entity& entity);
    vec2 MatchVelocityBoid(Entity& entity);
    vec2 ChasePlayerBoid(Entity& entity);
    void AvoidWallsBoid(Entity& entity);
};
