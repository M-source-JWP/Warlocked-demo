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

void AISystem::Step(float elapsedMs, RenderSystem *renderer)
{
    InitializeStatus();

    for (Entity entity : registry.hasAIs.entities) {
        ProcessAI(entity);
    }

    for (Entity entity: registry.hasAIs.entities) {
        UpdateEntityMovement(entity);
    }

    HandleEnemyAttacks(renderer);
}

void AISystem::InitializeStatus()
{
    playerEntity = registry.players.entities[0];
    status->playerEntity = &playerEntity;
}

void AISystem::ProcessAI(Entity entity)
{
    HasAI& ai = registry.hasAIs.get(entity);
    RemoveEnemyAttackIfPresent(entity);

    UpdateAIStatus(entity, ai);

    if (ai.type == AIType::Skeleton || ai.type == AIType::MiniBoss) {
        skeletonCurrentNode = skeletonCurrentNode->run();
    } else if (ai.type == AIType::Goblin) {
        UpdateGoblinBehavior(entity);
        goblinCurrentNode = goblinCurrentNode->run();
    } else if (ai.type == AIType::Mushroom) {
        mushroomCurrentNode = mushroomCurrentNode->run();
    }
}

void AISystem::RemoveEnemyAttackIfPresent(Entity entity)
{
    if (registry.enemyAttacks.has(entity)) {
        registry.enemyAttacks.remove(entity);
    }
}

void AISystem::UpdateAIStatus(Entity entity, HasAI& ai)
{
    status->aiMotion = &registry.motions.get(entity);
    status->playerMotion = &registry.motions.get(playerEntity);
    status->playerNearby = IsNearby(*status->aiMotion, *status->playerMotion, ai.detectionRadius);
    status->playerAttackable = IsNearby(*status->aiMotion, *status->playerMotion, registry.enemies.get(entity).attackRadius);
    status->aiEntity = &entity;
    status->shouldAttack = true;

    GenerateRandomNumbers();
}

void AISystem::UpdateGoblinBehavior(Entity entity)
{
    for (Entity otherEntity : registry.hasAIs.entities) {
        status->shouldAttack = false;
        if (otherEntity != entity) {
            if (IsNearby(*status->playerMotion, registry.motions.get(otherEntity), 120)) {
                status->shouldAttack = true;
                break;
            }
        }
    }
}

void AISystem::GenerateRandomNumbers()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distRand(1, 1000);
    std::uniform_int_distribution<> distRand2(1, 1000);
    status->rand = distRand(gen);
    status->rand2 = distRand2(gen);
}

void AISystem::UpdateEntityMovement(Entity entity)
{
    HasAI& ai = registry.hasAIs.get(entity);
    if (ai.type == AIType::Bat) {
        MoveBoid(entity);
    }
}

bool AISystem::IsNearby(Motion& motion1, Motion& motion2, float nearbyRadius) {
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

void AISystem::HandleEnemyAttacks(RenderSystem* renderer) {
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

void AISystem::MoveBoid(Entity& entity) {
    vec2 groupVector = GroupBoid(entity);
    vec2 separateVector = SeparateBoid(entity);
    vec2 matchVelocityVector = MatchVelocityBoid(entity);
    vec2 chaseVector = ChasePlayerBoid(entity);

    Motion& boidMotion = registry.motions.get(entity);
    vec2 newVel = boidMotion.velocity + (groupVector + separateVector + matchVelocityVector + chaseVector) / vec2(40, 40);

    newVel = Normalize(newVel) * vec2(boidMotion.speed, boidMotion.speed);
    boidMotion.velocity = newVel;

    Motion& playerMotion = registry.motions.get(playerEntity);
    HasAI& ai = registry.hasAIs.get(entity);
    if (!IsNearby(playerMotion, boidMotion, ai.detectionRadius)) {
        AvoidWallsBoid(entity);
    }
}

vec2 AISystem::GroupBoid(Entity& entity) {
    int batCount = 0;
    vec2 averagePos = {0, 0};
    Motion& entityMotion = registry.motions.get(entity);

    for (Entity otherEntity : registry.hasAIs.entities) {
        if (ShouldConsiderForGrouping(otherEntity, entity)) {
            batCount++;
            averagePos += registry.motions.get(otherEntity).position;
        }
    }

    return (batCount > 1) ? CalculateGroupVector(entityMotion, averagePos, batCount) : vec2(0, 0);
}

vec2 AISystem::SeparateBoid(Entity& entity) {
    int batCount = 0;
    vec2 endVelocity = {0, 0};
    Motion& entityMotion = registry.motions.get(entity);

    for (Entity otherEntity : registry.hasAIs.entities) {
        if (ShouldSeparateFrom(otherEntity, entity)) {
            endVelocity -= (entityMotion.position - registry.motions.get(otherEntity).position);
            batCount++;
        }
    }

    return (batCount > 1) ? CalculateSeparateVector(entityMotion, endVelocity, batCount) : vec2(0, 0);
}

vec2 AISystem::MatchVelocityBoid(Entity& entity) {
    int batCount = 0;
    vec2 averageVelocity = {0, 0};
    Motion& entityMotion = registry.motions.get(entity);

    for (Entity otherEntity : registry.hasAIs.entities) {
        if (ShouldConsiderForGrouping(otherEntity, entity)) {
            batCount++;
            averageVelocity += registry.motions.get(otherEntity).velocity;
        }
    }

    return (batCount > 1) ? CalculateMatchVelocityVector(entityMotion, averageVelocity, batCount) : vec2(0, 0);
}

vec2 AISystem::ChasePlayerBoid(Entity& entity) {
    Motion& entityMotion = registry.motions.get(entity);
    Motion& playerMotion = registry.motions.get(playerEntity);
    HasAI& ai = registry.hasAIs.get(entity);

    if (!IsNearby(entityMotion, playerMotion, ai.detectionRadius)) {
        return vec2(0, 0);
    }

    return CalculateChaseVector(entityMotion, playerMotion, ai);
}

void AISystem::AvoidWallsBoid(Entity& entity) {
    Motion& entityMotion = registry.motions.get(entity);
    AdjustVelocityForWalls(entityMotion);
}

// Helpers
vec2 AISystem::CalculateMatchVelocityVector(Motion& entityMotion, vec2 averageVelocity, int batCount) {
    averageVelocity = averageVelocity / vec2(batCount - 1, batCount - 1);
    return Normalize(averageVelocity - entityMotion.velocity) * vec2(entityMotion.speed, entityMotion.speed) * BOID_MATCH_RATIO / vec2(50, 50);
}

vec2 AISystem::CalculateChaseVector(Motion& entityMotion, Motion& playerMotion, HasAI& ai) {
    vec2 diff = playerMotion.position - entityMotion.position;
    float ratio = entityMotion.speed / sqrt(pow(diff.x, 2) + pow(diff.y, 2));
    return vec2(ratio * diff.x, ratio * diff.y) * vec2(BOID_CHASE_RATIO, BOID_CHASE_RATIO);
}

void AISystem::AdjustVelocityForWalls(Motion& entityMotion) {
    float dx = windowWidthPx - entityMotion.position.x;
    float dy = windowHeightPx - entityMotion.position.y;

    if (dx < BOID_WALL_AVOID_DIST) {
        entityMotion.velocity = vec2(-entityMotion.speed, entityMotion.velocity.y);
    } else if (windowWidthPx - dx < BOID_WALL_AVOID_DIST) {
        entityMotion.velocity = vec2(entityMotion.speed, entityMotion.velocity.y);
    }

    if (dy < BOID_WALL_AVOID_DIST) {
        entityMotion.velocity = vec2(entityMotion.velocity.x, -entityMotion.speed);
    } else if (windowHeightPx - dy < BOID_WALL_AVOID_DIST) {
        entityMotion.velocity = vec2(entityMotion.velocity.x, entityMotion.speed);
    }
}

bool AISystem::ShouldConsiderForGrouping(Entity otherEntity, Entity entity) {
    Motion& otherEntityMotion = registry.motions.get(otherEntity);
    HasAI& otherEntityAI = registry.hasAIs.get(otherEntity);
    Motion& entityMotion = registry.motions.get(entity);

    return otherEntity != entity &&
           otherEntityAI.type == AIType::Bat &&
           IsNearby(otherEntityMotion, entityMotion, BOID_GROUPING_RADIUS);
}

bool AISystem::ShouldSeparateFrom(Entity otherEntity, Entity entity) {
    Motion& otherEntityMotion = registry.motions.get(otherEntity);
    HasAI& otherEntityAI = registry.hasAIs.get(otherEntity);
    Motion& entityMotion = registry.motions.get(entity);

    return otherEntity != entity &&
           otherEntityAI.type == AIType::Bat &&
           IsNearby(entityMotion, otherEntityMotion, 50);
}

vec2 AISystem::CalculateGroupVector(Motion& entityMotion, vec2 averagePos, int batCount) {
    vec2 distanceToCenter = (averagePos / vec2(batCount - 1, batCount - 1)) - entityMotion.position;
    return distanceToCenter * BOID_GROUP_RATIO / vec2(50, 50);
}

vec2 AISystem::CalculateSeparateVector(Motion& entityMotion, vec2 endVelocity, int batCount) {
    endVelocity = endVelocity / vec2(batCount - 1, batCount - 1);
    return Normalize(endVelocity) * vec2(entityMotion.speed, entityMotion.speed) * -BOID_SEPERATE_RATIO;
}

vec2 AISystem::Normalize(vec2 vector) {
    float magnitude = sqrt(pow(vector.x, 2) + pow(vector.y, 2));
    magnitude = std::max(0.001f, magnitude);
    return vector / vec2(magnitude, magnitude);
}

AISystem::AISystem()
{
    status = new AIStatus();

    skeletonCurrentNode = CreateSkeletonBehaviorTree();
    goblinCurrentNode = CreateGoblinBehaviorTree();
    mushroomCurrentNode = CreateMushroomBehaviorTree();
}

//Tree creation functions
Node* AISystem::CreateSkeletonBehaviorTree() {
    Selector* root = CreateRootNode();

    Patrol* patrol = CreatePatrolNode(root);
    Sequence* chaseSequence = CreateChaseSequenceNode(root, {new ChasePlayer(), new AttackPlayer()});

    root->addChild(patrol);
    root->addChild(chaseSequence);

    return root;
}

Node* AISystem::CreateGoblinBehaviorTree() {
    Selector* root = CreateRootNode();

    Patrol* patrol = CreatePatrolNode(root);
    Sequence* playerSpotSequence = CreateSequenceNode(root);

    StalkPlayer* stalk = CreateStalkPlayerNode(playerSpotSequence);
    Sequence* chaseSequence = CreateChaseSequenceNode(playerSpotSequence, {new ChasePlayer(), new AttackPlayer()});

    playerSpotSequence->addChild(stalk);
    playerSpotSequence->addChild(chaseSequence);

    root->addChild(patrol);
    root->addChild(playerSpotSequence);

    return root;
}

Node* AISystem::CreateMushroomBehaviorTree() {
    Selector* root = CreateRootNode();

    Patrol* patrol = CreatePatrolNode(root);
    Sequence* chaseSequence = CreateChaseSequenceNode(root, {new ChasePlayer(), new AttackPlayer()});

    root->addChild(patrol);
    root->addChild(chaseSequence);

    return root;
}

//Tree creation helpers
Selector* AISystem::CreateRootNode() {
    Selector* root = new Selector();
    root->setParent(root);
    return root;
}

Patrol* AISystem::CreatePatrolNode(Node* parent) {
    Patrol* patrol = new Patrol();
    patrol->setParent(parent);
    patrol->setStatus(status);
    return patrol;
}

Sequence* AISystem::CreateChaseSequenceNode(Node* parent, std::initializer_list<Node*> children) {
    Sequence* sequence = new Sequence();
    sequence->setParent(parent);

    for (Node* child : children) {
        child->setParent(sequence);
        child->setStatus(status);
        sequence->addChild(child);
    }

    return sequence;
}

Sequence* AISystem::CreateSequenceNode(Node* parent) {
    Sequence* sequence = new Sequence();
    sequence->setParent(parent);
    return sequence;
}

StalkPlayer* AISystem::CreateStalkPlayerNode(Node* parent) {
    StalkPlayer* stalk = new StalkPlayer();
    stalk->setParent(parent);
    stalk->setStatus(status);
    return stalk;
}
