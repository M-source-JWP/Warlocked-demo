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
	Motion* ai_motion = nullptr;
	Motion* player_motion = nullptr;
	Entity* ai_entity = nullptr;
	Entity* player_entity = nullptr;
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
					status->ai_motion->velocity = vec2(0,0);
				}
				if (status->rand >= 99 && status->rand <= 100) {
					//make mobs move slower than their base speed when patrolling so they "run" at the player when spotting
					float xvel = ((status->rand % 10) - 4.5) * status->ai_motion->speed/9;
					float yvel = ((status->rand2 % 10) - 4.5) * status->ai_motion->speed/9;
					status->ai_motion->velocity = vec2(xvel,yvel);
				}
				this->state = NodeState::Running;
				return this;
			}
		}
};

class ChasePlayer : public LeafNode{
	public:
		Node* run()  {
			Motion* enemy_motion = status->ai_motion;
			Motion* player_motion = status->player_motion;
			//Case 1: Enemy has reached player, return true and move to attack node
			if (status->playerAttackable && status->shouldAttack) {
				this->state = NodeState::True;
				return this->parent;
			//Case 2: Enemy can see player but has not reached them, return running and keep chasing
			} else if (status->playerNearby && status->shouldAttack) {

				//move towards the player
				float ratio = enemy_motion->speed / sqrt(pow(player_motion->position.x - enemy_motion->position.x, 2) + pow(player_motion->position.y - enemy_motion->position.y, 2));
				enemy_motion->velocity = vec2(ratio * (player_motion->position.x - enemy_motion->position.x), ratio * (player_motion->position.y - enemy_motion->position.y));

				this->state = NodeState::Running;
				return this;
			//Case 3: Enemy has lost sight of player or should no longer be attacking, return to patrol loop
			} else { 
				//set velocity to half so enemies visibly stop "running" after losing the player
				enemy_motion->velocity = enemy_motion->velocity/vec2(2,2);
				this->state = NodeState::False;
				return this->parent;
			}
		}

};

class AttackPlayer : public LeafNode {
public:
	Node* run() {
		if (status->playerAttackable && status->shouldAttack) {
			if (!registry.enemyAttacks.has(*status->ai_entity)) registry.enemyAttacks.emplace(*status->ai_entity);
			this->state = NodeState::Running;
			return this;
		}
		else {
			if (registry.enemyAttacks.has(*status->ai_entity)) registry.enemyAttacks.remove(*status->ai_entity);
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
		Player& player = registry.players.get(*status->player_entity);
		float playerDist = sqrtf(pow((status->ai_motion->position.x-status->player_motion->position.x), 2)  + pow((status->ai_motion->position.y - status->player_motion->position.y), 2));
		HasAI& ai = registry.hasAIs.get(*status->ai_entity);

		Motion* enemy_motion = status->ai_motion;
		Motion* player_motion = status->player_motion;

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
				float ratio = enemy_motion->speed / sqrt(pow(player_motion->position.x - enemy_motion->position.x, 2) + pow(player_motion->position.y - enemy_motion->position.y, 2));
				enemy_motion->velocity = vec2(ratio * (player_motion->position.x - enemy_motion->position.x), ratio * (player_motion->position.y - enemy_motion->position.y));

				this->state = NodeState::Running;
				return this;

		} else if (playerDist > ai.detectionRadius * 0.55 && playerDist <= ai.detectionRadius * 0.6) {
			//Keep same position but face player
				float ratio = 0.0001 / sqrt(pow(player_motion->position.x - enemy_motion->position.x, 2) + pow(player_motion->position.y - enemy_motion->position.y, 2));
				enemy_motion->velocity = vec2(ratio * (player_motion->position.x - enemy_motion->position.x), ratio * (player_motion->position.y - enemy_motion->position.y));
				this->state = NodeState::Running;
				return this;	

		} else if (playerDist <= ai.detectionRadius * 0.55) {
				//move away from the player
				float ratio = -enemy_motion->speed / sqrt(pow(player_motion->position.x - enemy_motion->position.x, 2) + 
														  pow(player_motion->position.y - enemy_motion->position.y, 2));
				enemy_motion->velocity = vec2(ratio * (player_motion->position.x - enemy_motion->position.x), 
											  ratio * (player_motion->position.y - enemy_motion->position.y));
				this->state = NodeState::Running;
				return this;
		}
		//should never return here
		printf("ERROR StalkPlayer defaulted!");
		this->state = NodeState::False;
		return this->parent;
	}
};

class AISystem
{
private:
	Entity player_entity;
	Node* skeleton_current_node;
	Node* goblin_current_node;
	Node* mushroom_current_node;
	std::default_random_engine rng;
	std::uniform_real_distribution<float> uniform_dist; // number between 0..1
public:
	//one ai status for all entities - continually updated
	AIStatus* status;
	void step(float elapsed_ms, RenderSystem* renderer);
	void handle_enemy_attacks(RenderSystem* renderer);
	void attack_player(Entity damagingEnemy, float damage, RenderSystem* renderer);
	bool isNearby(Motion& motion1, Motion& motion2, float nearbyRadius);
	void move_boid(Entity& e);
	vec2 group_boid(Entity& e);
	vec2 seperate_boid(Entity& e);
 	vec2 match_velocity_boid(Entity& e);
	vec2 chase_player_boid(Entity& e);
	void avoid_walls_boid(Entity& e);
	AISystem();
};
