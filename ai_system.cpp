// internal
#include "ai_system.hpp"
#include "world_init.hpp"
#include "physics_system.hpp"
#include <algorithm>
float BOID_GROUPING_RADIUS = 280;
float BOID_WALL_AVOID_DIST = 40;
//ratios
float BOID_GROUP_RATIO = 1;
float BOID_SEPERATE_RATIO = 1;
float BOID_MATCH_RATIO = 1;
float BOID_CHASE_RATIO = 3;

void AISystem::step(float elapsed_ms, RenderSystem *renderer)
{
	// bug tree spaced in manner of it's hierarchy

    ComponentContainer<Motion> &motion_container = registry.motions;
	player_entity = registry.players.entities[0];
	status->player_entity = &player_entity;
	for (Entity entity : registry.hasAIs.entities) {
		HasAI& ai = registry.hasAIs.get(entity);
		if (registry.enemyAttacks.has(entity)) registry.enemyAttacks.remove(entity);
		//make sure this isn't called on non-enemies if we ever implement NPCs with AIs
		Enemy& enemy = registry.enemies.get(entity);
		status->ai_motion = &registry.motions.get(entity);
		status->player_motion = &registry.motions.get(player_entity);
		status->playerNearby = isNearby(*status->ai_motion, *status->player_motion, ai.detectionRadius);
		status->playerAttackable = isNearby(*status->ai_motion, *status->player_motion, enemy.attackRadius);
		status->ai_entity = &entity;
		status->shouldAttack = true;

		std::random_device rd; // obtain a random number from hardware
		std::mt19937 gen(rd()); // seed the generator
		std::uniform_int_distribution<> distRand(1, 1000);
		std::uniform_int_distribution<> distRand2(1, 1000); 
		status->rand = distRand(gen);
		status->rand2 = distRand2(gen);

		if (ai.type == AIType::Skeleton || ai.type == AIType::MiniBoss) {
			skeleton_current_node = skeleton_current_node->run();

		//goblin "other enemy" cooperative behavior handled here
		} else if (ai.type == AIType::Goblin) {
		for (Entity entity : registry.hasAIs.entities) {
			status->shouldAttack = false;
			if (entity != *status->ai_entity) {
				Motion& other_enemy_motion = registry.motions.get(entity);

				//if other enemy is nearby (and thus attacking) return true
				if (isNearby(*status->player_motion,other_enemy_motion,120)) {
					status->shouldAttack = true;
					break;
				}
			}
		}
			goblin_current_node = goblin_current_node->run();
		} else if (ai.type == AIType::Mushroom) {
			mushroom_current_node = mushroom_current_node->run();
	}
	}

	for (Entity entity: registry.hasAIs.entities) {
		HasAI& ai = registry.hasAIs.get(entity);
		 if (ai.type == AIType::Bat) {
			move_boid(entity);
		}
	}
	handle_enemy_attacks(renderer);
}


bool AISystem::isNearby(Motion& motion1, Motion& motion2, float nearbyRadius) {
	float dist = sqrtf(pow((motion2.position.x-motion1.position.x), 2)  + pow((motion2.position.y - motion1.position.y), 2));
	return (dist <= nearbyRadius);
}


void AISystem::attack_player(Entity damagingEnemy, float damage, RenderSystem* renderer) {
	Player& curr_player = registry.players.get(player_entity);
	Motion& player_motion = registry.motions.get(player_entity);
	Motion& enemy_motion = registry.motions.get(damagingEnemy);
	Enemy& enemy_component = registry.enemies.get(damagingEnemy);

	float ex = enemy_motion.position.x;
	float ey = enemy_motion.position.y;

	const float ENEMY_AS_MS = 231 * enemy_component.attackCoolDown;

	float xDiff = player_motion.position.x - ex;
	float yDiff = player_motion.position.y - ey;

	if (xDiff > 0) {
		enemy_motion.attackDirection = enemy_motion.RIGHT;
		if (enemy_motion.scale.x < 1) {
			enemy_motion.scale.x *= -1;
			enemy_motion.direction = enemy_motion.RIGHT;
		}
	}
	else {
		enemy_motion.attackDirection = enemy_motion.LEFT;
		if (enemy_motion.scale.x > 1) {
			enemy_motion.scale.x *= -1;
			enemy_motion.direction = enemy_motion.LEFT;
		}
	}
	float enemy_attack_offset = 40;
	if (enemy_motion.scale.x > 0) {
		createEnemyAttack(renderer, vec2(ex + enemy_attack_offset, ey), enemy_component.damagePerAttack, 900, damagingEnemy);
	}
	else {
		createEnemyAttack(renderer, vec2(ex - enemy_attack_offset, ey), enemy_component.damagePerAttack, 900, damagingEnemy);
	}
}

void AISystem::handle_enemy_attacks(RenderSystem* renderer) {
	if (!registry.deathTimers.has(player_entity)) {
		for (uint i = 0; i < registry.enemyAttacks.components.size(); i++) {
			Entity entity_enemy = registry.enemyAttacks.entities[i];
			Enemy& enemy = registry.enemies.get(entity_enemy);
			Motion& enemyMotion = registry.motions.get(entity_enemy);
			// attack animation
			if (!registry.attackCoolDown.has(entity_enemy) && !registry.deathTimers.has(entity_enemy)) {
				enemyMotion.attacking = true;
				enemyMotion.attackDirection = enemyMotion.RIGHT;
				attack_player(entity_enemy, enemy.damagePerAttack, renderer);
				AttackTimer timer = { enemy.attackCoolDown };
				registry.attackCoolDown.insert(entity_enemy, timer, false);

				enemyMotion.fc = 0;
				registry.renderRequests.remove(entity_enemy);
				registry.renderRequests.insert(entity_enemy, { enemy.attackTexture,
					EFFECT_ASSET_ID::DEFAULT_ANIMATION,
					GEOMETRY_BUFFER_ID::SPRITE });
			}
			else if (enemyMotion.attacking == false) {
				if (enemyMotion.velocity.x > 0) {
					enemyMotion.scale = { abs(enemyMotion.scale.x) , enemyMotion.scale.y };
					enemyMotion.direction = enemyMotion.RIGHT;
				}
				else {
					enemyMotion.scale = { -abs(enemyMotion.scale.x) , enemyMotion.scale.y };
					enemyMotion.direction = enemyMotion.LEFT;
				}
				registry.renderRequests.remove(entity_enemy);
				registry.renderRequests.insert(entity_enemy, { enemy.movementTexture,
					EFFECT_ASSET_ID::DEFAULT_ANIMATION,
					GEOMETRY_BUFFER_ID::SPRITE });
				registry.enemyAttacks.remove(entity_enemy);
			}
		}
	}
}


void AISystem::move_boid(Entity& e) {
	vec2 group_vector = group_boid(e);
	vec2 seperate_vector = seperate_boid(e);
	vec2 match_velocity_vector = match_velocity_boid(e);
	vec2 chase_vector = chase_player_boid(e);

	Motion& boid_motion = registry.motions.get(e);
	vec2 new_vel = boid_motion.velocity + (group_vector + seperate_vector + match_velocity_vector + chase_vector)/vec2(40,40);

	float magnitude = sqrt(pow(new_vel.x, 2) + pow(new_vel.y, 2));
	magnitude = std::max(0.001f, magnitude);
	new_vel = new_vel / vec2(magnitude,magnitude);
	new_vel = new_vel * vec2(boid_motion.speed,boid_motion.speed);
	boid_motion.velocity = new_vel;

	Motion& player_motion = registry.motions.get(player_entity);
	HasAI& ai = registry.hasAIs.get(e);
	if (!isNearby(player_motion,boid_motion,ai.detectionRadius)) {
	avoid_walls_boid(e);
	}
}

vec2 AISystem::group_boid(Entity& e) {
	int batcount = 0;
	vec2 average_pos = {0,0};
	Motion& entity_motion = registry.motions.get(e);
	for (Entity other_entity : registry.hasAIs.entities) {
		HasAI& other_entity_ai = registry.hasAIs.get(other_entity);
		Motion& other_entity_motion = registry.motions.get(other_entity);
		if (other_entity == e || other_entity_ai.type != AIType::Bat || !isNearby(other_entity_motion,entity_motion,BOID_GROUPING_RADIUS)) continue;
		batcount += 1;
		average_pos += other_entity_motion.position;
	}
	if (batcount > 1) {
	vec2 distance_to_center = (average_pos/vec2(batcount - 1,batcount - 1)) - entity_motion.position;
	
	//divide by 80 so boids don't immediately teleport to the center of mass of the swarm
	return distance_to_center * BOID_GROUP_RATIO / vec2(50,50);
	}
	else return vec2(0,0);
}

vec2 AISystem::seperate_boid(Entity& e) {
	vec2 end_velocity = {0,0};
	int batcount = 0;
	Motion& entity_motion = registry.motions.get(e);
	for (Entity other_entity : registry.hasAIs.entities) {
		HasAI& other_entity_ai = registry.hasAIs.get(other_entity);
		if (other_entity == e || other_entity_ai.type != AIType::Bat) continue;

		Motion& other_entity_motion = registry.motions.get(other_entity);
		if (isNearby(entity_motion,other_entity_motion,50)) {
			end_velocity -= (entity_motion.position - other_entity_motion.position);
			batcount += 1;
		}
	}
	if (batcount > 1) {
	end_velocity = end_velocity / vec2(batcount - 1,batcount - 1);
	float magnitude = sqrt((end_velocity.x * end_velocity.x) + (end_velocity.y * end_velocity.y));
	magnitude = std::max(0.001f, magnitude);
	end_velocity = end_velocity / vec2(magnitude,magnitude);
	end_velocity = end_velocity * vec2(entity_motion.speed,entity_motion.speed);
	}
	return -end_velocity * BOID_SEPERATE_RATIO;
}

vec2 AISystem::match_velocity_boid(Entity& e) {
	int batcount = 0;
	vec2 average_velocity = {0,0};
	Motion& entity_motion = registry.motions.get(e);
	for (Entity other_entity : registry.hasAIs.entities) {
		Motion& other_entity_motion = registry.motions.get(other_entity);
		HasAI& other_entity_ai = registry.hasAIs.get(other_entity);
		if (other_entity == e || other_entity_ai.type != AIType::Bat || !isNearby(entity_motion,other_entity_motion,BOID_GROUPING_RADIUS)) continue;
		batcount += 1;
		average_velocity += other_entity_motion.velocity;
	}
	if (batcount > 1) { 
	average_velocity = average_velocity/vec2(batcount - 1,batcount - 1);
	float magnitude = sqrt(pow(average_velocity.x, 2) + pow(average_velocity.y, 2));
	magnitude = std::max(0.001f, magnitude);
	average_velocity = average_velocity / vec2(magnitude,magnitude);
	average_velocity = average_velocity * vec2(entity_motion.speed,entity_motion.speed);

	return (average_velocity - registry.motions.get(e).velocity) * BOID_MATCH_RATIO /vec2(50,50);
	} else return vec2(0,0);
}

vec2 AISystem::chase_player_boid(Entity& e) {
	Motion& entity_motion = registry.motions.get(e);
	Motion& player_motion = registry.motions.get(player_entity);
	HasAI& ai = registry.hasAIs.get(e);
	vec2 diff = player_motion.position - entity_motion.position;
	if (!isNearby(entity_motion,player_motion,ai.detectionRadius)) return vec2(0,0);
	if (diff.x == 0 || diff.y == 0) {
		return vec2(0,0);
	}
	float ratio = entity_motion.speed / sqrt(pow(player_motion.position.x - entity_motion.position.x, 2) + pow(player_motion.position.y - entity_motion.position.y, 2));
	return vec2(ratio * (player_motion.position.x - entity_motion.position.x), ratio * (player_motion.position.y - entity_motion.position.y)) * vec2(BOID_CHASE_RATIO,BOID_CHASE_RATIO);
}

void AISystem::avoid_walls_boid(Entity& e) {
	Motion& entity_motion = registry.motions.get(e);
	float dx = window_width_px - entity_motion.position.x;
	float dy = window_height_px - entity_motion.position.y;
	vec2 out_vector = {0,0};
	if (dx < BOID_WALL_AVOID_DIST) {
		entity_motion.velocity = vec2(-entity_motion.speed,entity_motion.velocity.y);
	}
	if (BOID_WALL_AVOID_DIST > window_width_px - dx) {
		entity_motion.velocity = vec2(entity_motion.speed,entity_motion.velocity.y);
	}
	if (dy < BOID_WALL_AVOID_DIST) {
		entity_motion.velocity = vec2(entity_motion.velocity.x,-entity_motion.speed);
	}
	if (BOID_WALL_AVOID_DIST > window_height_px - dy) {
		entity_motion.velocity = vec2(entity_motion.velocity.x,entity_motion.speed);
	}

}

AISystem::AISystem()
{
	//we only need one of these - it only stores information relevant to the current ai and it's continually updated 
	status = new AIStatus();

	//behavior tree diagrams included in Work Samples submission for clarity

	//Skeleton behavior tree:
	Selector* SKRoot = new Selector();
	//Roots are their own parents so they can loop on themselves when the game steps
	SKRoot->setParent(SKRoot);

		Patrol* SKPatrol = new Patrol();
		SKPatrol->setParent(SKRoot);
		SKPatrol->setStatus(status);

		Sequence* SKChaseSequence = new Sequence();
		SKChaseSequence->setParent(SKRoot);

			ChasePlayer* SKChase = new ChasePlayer();
			SKChase->setParent(SKChaseSequence);
			SKChase->setStatus(status);

			AttackPlayer* SKAttack = new AttackPlayer();
			SKAttack->setParent(SKChaseSequence);
			SKAttack->setStatus(status);


	SKRoot->addChild(SKPatrol);
	SKRoot->addChild(SKChaseSequence);
	SKChaseSequence->addChild(SKChase);
	SKChaseSequence->addChild(SKAttack);
	skeleton_current_node = SKRoot;

	//goblin behavior tree:
	Selector* GBRoot = new Selector();
	GBRoot->setParent(GBRoot);

		Patrol* GBPatrol = new Patrol();
		GBPatrol->setParent(GBRoot);
		GBPatrol->setStatus(status);

		Sequence* GBPlayerSpotSequence = new Sequence();
		GBPlayerSpotSequence->setParent(GBRoot);

			StalkPlayer* GBStalk = new StalkPlayer();
			GBStalk->setParent(GBPlayerSpotSequence);
			GBStalk->setStatus(status);

			Sequence* GBChaseSequence = new Sequence();
			GBChaseSequence->setParent(GBPlayerSpotSequence);

				ChasePlayer* GBChase = new ChasePlayer();
				GBChase->setParent(GBChaseSequence);
				GBChase->setStatus(status);

				AttackPlayer* GBAttack = new AttackPlayer();
				GBAttack->setParent(GBChaseSequence);
				GBAttack->setStatus(status);

			GBChaseSequence->addChild(GBChase);
			GBChaseSequence->addChild(GBAttack);

		GBPlayerSpotSequence->addChild(GBStalk);
		GBPlayerSpotSequence->addChild(GBChaseSequence);
	
	GBRoot->addChild(GBPatrol);
	GBRoot->addChild(GBPlayerSpotSequence);
	goblin_current_node = GBRoot;


	//Mushroom behavior tree:
	Selector* MSRoot = new Selector();
	MSRoot->setParent(MSRoot);

		Patrol* MSPatrol = new Patrol();
		MSPatrol->setParent(MSRoot);
		MSPatrol->setStatus(status);

		Sequence* MSChaseSequence = new Sequence();
		MSChaseSequence->setParent(MSRoot);

			ChasePlayer* MSChase = new ChasePlayer();
			MSChase->setParent(MSChaseSequence);
			MSChase->setStatus(status);

			AttackPlayer* MSAttack = new AttackPlayer();
			MSAttack->setParent(MSChaseSequence);
			MSAttack->setStatus(status);


	MSRoot->addChild(MSPatrol);
	MSRoot->addChild(MSChaseSequence);
	MSChaseSequence->addChild(MSChase);
	MSChaseSequence->addChild(SKAttack);
	mushroom_current_node = MSRoot;
}
