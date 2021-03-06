#include <algorithm>
#include <cmath>
#include <GL/glu.h>
#include <Box2D.h>

#include "world.hh"
#include "player.hh"
#include "util.hh"
#include "texture.hh"
#include "powerups.hh"

#ifdef USE_THREADS
	#define LOCKMUTEX boost::mutex::scoped_lock lock(mutex)
#else
	#define LOCKMUTEX
#endif

namespace {

	static const unsigned SUPER_MAX_POWERUPS = 5; // Game mode cannot go over this
	static const float offset = 3.0; // For spawning things away from borders

	enum ElementType   { NONE, BORDER, WATER, PLATFORM, LADDER, CRATE, BRIDGE, POWERUP, ACTOR, MINE };
	static ElementType
	  ElementTypes[] = { NONE, BORDER, WATER, PLATFORM, LADDER, CRATE, BRIDGE, POWERUP, ACTOR, MINE };
	struct WorldElement {
		WorldElement(ElementType type, void* element = NULL): type(type), ptr(element) { }
		ElementType type;
		void* ptr;
	};
	// This class captures the closest hit shape.
	struct RayCastCallback: public b2RayCastCallback {
		RayCastCallback(): m_fixture(NULL) { }
		float32 ReportFixture(b2Fixture* fixture, const b2Vec2& point,
							  const b2Vec2& normal, float32 fraction) {
			m_fixture = fixture;
			m_point = point;
			m_normal = normal;
			m_fraction = fraction;
			return fraction;
		}
		b2Fixture* m_fixture;
		b2Vec2 m_point;
		b2Vec2 m_normal;
		float32 m_fraction;
	};
	// Checks for AABB collisions
	struct AABBQueryCallback: public b2QueryCallback {
		AABBQueryCallback(): m_fixture(NULL) { }
		bool ReportFixture(b2Fixture* fixture) { m_fixture = fixture; return false; }
		bool operator()() {return (m_fixture); }
		b2Fixture* m_fixture;
	};


	void addScore(Actors& pls, Actor* pl, int score) {
		if (!pl) return;
		if (score >= 0) pl->points.add(score);
		else {
			for (Actors::iterator it = pls.begin(); it != pls.end(); ++it) {
				if (&(*it) != pl) it->points.add(-score);
			}
		}
	}
}


World::World(int width, int height, TextureMap& tm, GameMode gm, bool master):
  is_master(master), world(b2Vec2(0.0f, 0.0f)), w(width), h(height),
  SCALE(16.0), view_topleft(0,0), view_bottomright(w,h),
  tilesize(1), water_height(2.5), timer_powerup(gm.getPowerupDelay()), game(gm)
{

	// Get texture IDs
	for (int i = 1; i <= 4; ++i) texture_player[i-1] = tm.find(std::string("tomato_") + num2str(i))->second;
	texture_background = tm.find("background")->second;
	texture_water = tm.find("water")->second;
	texture_ground = tm.find("ground")->second;
	texture_ladder = tm.find("ladder")->second;
	texture_crate = tm.find("crate")->second;
	texture_powerups = tm.find("powerups")->second;

	// Generate
	generateBorders();
	if (is_master) generateLevel();
	game.startRound();
}


Actor* World::shoot(const Actor& shooter) {
	RayCastCallback callback;
	b2Vec2 unitdir(shooter.dir, 0);
	b2Vec2 point1 = shooter.getBody()->GetWorldCenter() + 1.5 * shooter.getSize() * unitdir;
	b2Vec2 point2 = shooter.getBody()->GetWorldCenter() + w * unitdir;
	LOCKMUTEX;
	world.RayCast(&callback, point1, point2);

	if (!callback.m_fixture) return NULL;
	b2Body* b = callback.m_fixture->GetBody();
	if (!b || !b->GetUserData()) return NULL;
	Actor* hit = NULL;
	if (*(static_cast<ElementType*>(b->GetUserData())) == ACTOR) {
		b->SetUserData(NULL);
		// Find the actor
		for (Actors::iterator it = actors.begin(); it != actors.end(); ++it) {
			if (!it->getBody()->GetUserData()) {
				hit = &(*it);
				break;
			}
		}
		b->SetUserData(&ElementTypes[ACTOR]);
	}
	return hit;
}


void World::kill(Actor* target, Actor* killer) {
	if (!target) return;
	target->die();
	target->points.deaths++;
	if (killer) {
		addScore(actors, target, game.getKilledPoints());
		addScore(actors, killer, game.getKillerPoints());
		killer->points.kills++;
	} else addScore(actors, target, game.getSuicidePoints());

	// Check for kill limit
	if (game.getScoreLimit() > 0
	  && ( std::abs(target->points.round_score) >= game.getScoreLimit()
	   || (killer && std::abs(killer->points.round_score) >= game.getScoreLimit())))
	{
		game.end = true;
	}

	target->respawn = Countdown(game.getRespawnDelay());
	target->equip(game.getDefaultPowerup());

	std::cout << "DEATH! Points: " << target->points.round_score << std::endl;

}


bool World::safe2spawn(float x, float y) const {
	RayCastCallback callback;
	b2Vec2 unitdir(0, 1);
	world.RayCast(&callback, b2Vec2(x, y) + tilesize * unitdir, b2Vec2(x, y) + 5 * tilesize * unitdir);
	if (!callback.m_fixture) return false;
	b2Body* b = callback.m_fixture->GetBody();
	if (!b || !b->GetUserData()) return false;
	if (*(static_cast<ElementType*>(b->GetUserData())) == PLATFORM) return true;
	return false;
}


b2Vec2 World::randomSpawn() const {
	float x,y;
	do {
		x = randf(offset, w-offset);
		y = randf(offset, h*0.667);
	} while (!safe2spawn(x,y));
	return b2Vec2(x,y);
}


b2Vec2 World::randomSpawnLocked() const {
	LOCKMUTEX;
	return randomSpawn();
}


void World::addMine(float x, float y) {
	float minew = tilesize * 0.3f;
	float mineh = tilesize * 0.1f;
	// Create body
	b2BodyDef bodyDef;
	bodyDef.position.Set(x, y);
	LOCKMUTEX;
	b2Body* body = world.CreateBody(&bodyDef);
	body->SetUserData(&ElementTypes[MINE]);
	// Create shape
	b2PolygonShape box;
	box.SetAsBox(minew/2, mineh/2);
	// Create fixture
	b2FixtureDef fixtureDef;
	fixtureDef.shape = &box;
	fixtureDef.density = 1.0f;
	body->CreateFixture(&fixtureDef);
}


void World::addActor(float x, float y, Actor::Type type, int character, Client* client) {
	GLuint tex = texture_player[character - 1];
	Actor* actor;
	LOCKMUTEX;
	if (client) actor = new OnlinePlayer(client, tex, type);
	else actor = new Actor(tex, type);
	addActorBody(x, y, actor);
	actor->world = this;
	actor->equip(game.getDefaultPowerup());
	actors.push_back(actor);
}


void World::addActorBody(float x, float y, Actor* actor) {
	// Define the dynamic body. We set its position and call the body factory.
	b2BodyDef bodyDef;
	bodyDef.type = b2_dynamicBody;
	bodyDef.position.Set(x, y);
	bodyDef.fixedRotation = true;
	actor->body = world.CreateBody(&bodyDef);
	actor->body->SetUserData(&ElementTypes[ACTOR]);
	// Define a circle shape for our dynamic body.
	b2CircleShape circle;
	circle.m_radius = actor->getSize();
	// Define the dynamic body fixture.
	b2FixtureDef fixtureDef;
	fixtureDef.shape = &circle;
	fixtureDef.density = 0.75f; // Set the density to be non-zero, so it will be dynamic.
	fixtureDef.friction = PLAYER_FRICTION; // Friction
	fixtureDef.restitution = PLAYER_RESTITUTION; // Bounciness
	// Add the shape to the body.
	actor->getBody()->CreateFixture(&fixtureDef);
}


bool World::addPlatform(float x, float y, float w, bool force) {
	// Test for overlap
	b2AABB aabb;
	aabb.lowerBound = b2Vec2(x - tilesize, y - tilesize);
	aabb.upperBound = b2Vec2(x + w * tilesize + tilesize, y + tilesize + tilesize);
	LOCKMUTEX;
	if (!force) {
		AABBQueryCallback qc;
		world.QueryAABB(&qc, aabb);
		if (qc()) return false;
	}
	Platform p(w, texture_ground, 0, tilesize);
	// Create body
	b2BodyDef bodyDef;
	bodyDef.position = aabb.GetCenter();
	//bodyDef.position.Set(x, y);
	p.body = world.CreateBody(&bodyDef);
	p.body->SetUserData(&ElementTypes[PLATFORM]);
	// Create shape
	b2PolygonShape box;
	box.SetAsBox(w/2*tilesize, 0.5f*tilesize);
	// Create fixture
	b2FixtureDef fixtureDef;
	fixtureDef.shape = &box;
	fixtureDef.friction = 4.0f; // Higher friction
	p.body->CreateFixture(&fixtureDef);
	p.buildVertices();
	platforms.push_back(p);
	return true;
}


void World::addLadder(float x, float y, float h) {
	Ladder l(h, texture_ladder, 0, tilesize);
	// Create body
	b2BodyDef bodyDef;
	bodyDef.position.Set(x + tilesize*0.5f, y + h/2 * tilesize);
	LOCKMUTEX;
	l.body = world.CreateBody(&bodyDef);
	l.body->SetUserData(&ElementTypes[LADDER]);
	// Create shape
	b2PolygonShape laddershape;
	//laddershape.SetAsEdge(b2Vec2(0.5f*tilesize, y), b2Vec2(0.5f*tilesize, h));
	laddershape.SetAsBox(0.10f*tilesize, h/2*tilesize - tilesize * 0.5f);
	// Create fixture
	b2FixtureDef fixtureDef;
	fixtureDef.shape = &laddershape;
	fixtureDef.isSensor = true; // No collision response
	l.body->CreateFixture(&fixtureDef);
	l.buildVertices();
	ladders.push_back(l);
}


void World::addCrate(float x, float y) {
	Crate cr(texture_crate, 0, tilesize);
	// Define the dynamic body. We set its position and call the body factory.
	b2BodyDef bodyDef;
	bodyDef.type = b2_dynamicBody;
	bodyDef.position.Set(x, y);
	LOCKMUTEX;
	cr.body = world.CreateBody(&bodyDef);
	cr.body->SetUserData(&ElementTypes[CRATE]);

	// Define a shape for our dynamic body.
	b2PolygonShape box;
	box.SetAsBox(0.5f*tilesize, 0.5f*tilesize);

	// Define the dynamic body fixture.
	b2FixtureDef fixtureDef;
	fixtureDef.shape = &box;
	fixtureDef.density = 1.0f; // Set the density to be non-zero, so it will be dynamic.
	fixtureDef.friction = 0.5f;
	fixtureDef.restitution = 0.05f;
	cr.getBody()->CreateFixture(&fixtureDef);

	crates.push_back(cr);
}


void World::addBridge(unsigned leftAnchorID, unsigned rightAnchorID) {
	Platform leftAnchor = platforms.at(leftAnchorID);
	Platform rightAnchor = platforms.at(rightAnchorID);
	float segmentW = 0.5f * tilesize;
	float x1 = leftAnchor.getX() + leftAnchor.getW() * 0.5f - segmentW * 0.5f;
	float y1 = leftAnchor.getY() - leftAnchor.getH() * 0.5f + tilesize * 0.1f;
	float x2 = rightAnchor.getX() - rightAnchor.getW() * 0.5f + segmentW * 0.5f;
	float y2 = rightAnchor.getY() - rightAnchor.getH() * 0.5f + tilesize * 0.1f;
	int segments = distance(x1,y1,x2,y2) / segmentW + 1;
	segmentW = distance(x1,y1,x2,y2) / segments;
	float xstep = (x2-x1) / segments;
	float ystep = (y2-y1) / segments;
	Bridge bridge(leftAnchorID, rightAnchorID, 0, 0, tilesize);

	b2PolygonShape shape;
	shape.SetAsBox(segmentW * 0.5f, 0.05f * tilesize);

	b2FixtureDef fd;
	fd.shape = &shape;
	fd.density = 1.0f;
	fd.friction = 3.0f;
	fd.filter.categoryBits = 0x0002;

	b2RevoluteJointDef jd;
	b2Body* prevBody = leftAnchor.getBody();
	LOCKMUTEX;
	for (int i = 0; i < segments; ++i) {
		b2BodyDef bd;
		bd.type = b2_dynamicBody;
		bd.position.Set(x1 + xstep * i + segmentW * 0.5f, y1 + ystep * i);
		b2Body* body = world.CreateBody(&bd);
		body->SetUserData(&ElementTypes[BRIDGE]);
		body->CreateFixture(&fd);

		b2Vec2 anchor(x1 + xstep * i, y1 + ystep * i);
		jd.Initialize(prevBody, body, anchor);
		world.CreateJoint(&jd);

		bridge.bodies.push_back(body);
		prevBody = body;
	}

	jd.Initialize(prevBody, rightAnchor.getBody(), b2Vec2(x2, y2));
	world.CreateJoint(&jd);
	bridges.push_back(bridge);
}


void World::addPowerup(float x, float y, Powerup::Type type) {
	if (powerups.size() >= std::min((unsigned)game.getPowerupLimit(), SUPER_MAX_POWERUPS)) return;
	PowerupEntity pw(type, texture_powerups);
	// Define the dynamic body. We set its position and call the body factory.
	b2BodyDef bodyDef;
	bodyDef.type = b2_dynamicBody;
	bodyDef.position.Set(x, y);
	bodyDef.fixedRotation = true;
	LOCKMUTEX;
	pw.body = world.CreateBody(&bodyDef);
	pw.body->SetUserData(&ElementTypes[POWERUP]);

	// Define a circle shape for our dynamic body.
	b2CircleShape shape;
	shape.m_radius = pw.getSize() * 0.75f;

	// Define the dynamic body fixture.
	b2FixtureDef fixtureDef;
	fixtureDef.shape = &shape;
	fixtureDef.density = 0.1f; // Set the density to be non-zero, so it will be dynamic.
	fixtureDef.restitution = 1.0001f; // Over-full bounciness
	fixtureDef.friction = 0.0f; // No friction
	fixtureDef.filter.maskBits = 0xFFFD;
	pw.getBody()->CreateFixture(&fixtureDef);

	// Set a random velocity
	float a = randf(0.0f, 2*PI);
	float spd = randf(GRAVITY*0.5f, GRAVITY*1.5f);
	pw.getBody()->SetLinearVelocity(b2Vec2(cos(a)*spd, sin(a)*spd));

	powerups.push_back(pw);
}


void World::generateBorders() {
	LOCKMUTEX;
	float hw = w*0.5, hh = h*0.5;
	// Define the border bodies
	b2BodyDef borderBodyDef;
	borderBodyDef.position.Set(hw, hh);
	b2Body* borderBody = world.CreateBody(&borderBodyDef);
	// Define the border shapes
	b2EdgeShape borderBoxLeft, borderBoxRight, borderBoxTop, borderBoxBottom;
	borderBoxLeft.Set(b2Vec2(-hw,-hh), b2Vec2(-hw,hh));
	borderBoxRight.Set(b2Vec2(hw,-hh), b2Vec2(hw,hh));
	borderBoxTop.Set(b2Vec2(-hw,-hh), b2Vec2(hw,-hh));
	borderBoxBottom.Set(b2Vec2(-hw,hh), b2Vec2(hw,hh));
	// Add the border fixtures to the body
	borderBody->CreateFixture(&borderBoxLeft, 0.0f);
	borderBody->CreateFixture(&borderBoxRight, 0.0f);
	borderBody->CreateFixture(&borderBoxTop, 0.0f);
	borderBody->CreateFixture(&borderBoxBottom, 0.0f);
	borderBody->SetUserData(&ElementTypes[BORDER]);
	// Create water
	b2BodyDef waterBodyDef;
	waterBodyDef.position.Set(hw, h - water_height*0.5f);
	b2Body* waterBody = world.CreateBody(&waterBodyDef);
	b2PolygonShape waterBox;
	waterBox.SetAsBox(w*0.5f, water_height*0.5f);
	b2FixtureDef fixtureDef;
	fixtureDef.shape = &waterBox;
	fixtureDef.isSensor = true; // No collision response
	waterBody->CreateFixture(&fixtureDef);
	waterBody->SetUserData(&ElementTypes[WATER]);
}


void World::generateLevel() {
	float xoff = 1.5*tilesize;
	float yoff = 2.5*tilesize;
	// Create starting platforms
	float x = randf(xoff, xoff+tilesize);
	float y1 = randf(3*tilesize, 5*tilesize);
	float y2 = randf(h-8*tilesize, h-5*tilesize);
	int ytilediff = int((y2-y1) / tilesize) + 1;
	addPlatform(x + tilesize, y1, randint(2,4)); // Top left
	addPlatform(x, y2, randint(2,4)); // Bottom left
	addLadder(x, y2 - ytilediff * tilesize, ytilediff); // Connect with ladder
	addLadder(0, y2 - tilesize*0.333f, h - y2); // Left side ladder from water
	float w1 = randint(2,4);
	float w2 = randint(2,4);
	x = randf(w - xoff - tilesize - tilesize, w - xoff - tilesize);
	y1 = randf(3*tilesize,5*tilesize);
	y2 = randf(h-8*tilesize,h-5*tilesize);
	ytilediff = int((y2-y1) / tilesize) + 1;
	addPlatform(x - w1*tilesize - tilesize, y1, w1); // Top right
	addPlatform(x - w2*tilesize, y2, w2); // Bottom right
	addLadder(x - tilesize, y2 - ytilediff * tilesize, ytilediff); // Connect with ladder
	addLadder(w - tilesize, y2 - tilesize*0.333f, h - y2); // Right side ladder from water
	// Create rest of platforms
	for (int j = yoff; j < h - water_height - yoff; j += 4*tilesize) {
		int count = 0;
		for (int i = xoff + 6*tilesize; i < w - xoff - 6*tilesize; i += 7*tilesize) {
			int tries = 10;
			while (--tries > 0 && !addPlatform(i + randf(-3*tilesize,3*tilesize), j + randf(-tilesize,tilesize), randint(2,6)));
			if (tries > 0) count++;
		}
		if (count > 1) {
			count = randint(count-1);
			addBridge(platforms.size()-count-2, platforms.size()-count-1);
		}
	}
	//for (int i = 0; i < 4; i++) {
		//addLadder(randint(0,w), randint(0,h), randint(2,6));
	//}
	// Create crates
	for (int i = 0; i < 8; i++) {
		addCrate(randint(0,w), randint(0,h));
	}
}


void World::newRound() {
	// TODO: Proper game ending
	if (game.gameEnded()) {
		std::cout << "Game ended." << std::endl;
		exit(0);
	}
	// TODO: Show previous round winner etc.


	// FIXME: For some reason trying to generate new world crashes @ Platform::buildVertices
	/*
	{ 	LOCKMUTEX;
		platforms.clear();
		ladders.clear();
		crates.clear();
		bridges.clear();
		powerups.clear();
		// Reset physics world
		world = b2World(b2Vec2(), true);
	}
	generateBorders();
	generateLevel();
	*/
	{ 	LOCKMUTEX;
		for (Actors::iterator it = actors.begin(); it != actors.end(); ++it) {
			it->points.round_score = 0;
			b2Vec2 pos = randomSpawn();
			addActorBody(pos.x, pos.y, &(*it));
			it->dead = false;
		}
	}
	game.startRound();
}


void World::update() {
	// Prepare for simulation. Typically we use a time step of 1/60 of a
	// second (60Hz) and 10 iterations. This provides a high quality simulation
	// in most game scenarios.
	float32 timeStep = 1.0f / 100.0f;
	int32 velocityIterations = 10;
	int32 positionIterations = 10;

	double t = GetSecs();
	{
		LOCKMUTEX;

		// Instruct the world to perform a single step of simulation.
		// It is generally best to keep the time step and iterations fixed.
		world.Step(timeStep, velocityIterations, positionIterations);

		// Clear applied body forces. We didn't apply any forces, but you
		// should know about this function.
		world.ClearForces();

		// Update actors' airborne etc. status + gravity
		int alive_people = 0;
		for (Actors::iterator it = actors.begin(); it != actors.end(); ++it) {
			it->airborne = true;
			bool hitwall = false;
			bool climbing = (it->ladder == Actor::LADDER_CLIMBING);
			it->ladder = Actor::LADDER_NO;
			// Water
			if (!it->is_dead() && it->getBody()->GetWorldCenter().y >= h - water_height) kill(&(*it));
			// Death
			if (it->is_dead()) {
				it->getBody()->SetLinearVelocity(b2Vec2());
				// TODO: Disable physics
				if (game.getRespawnDelay() >= 0 && it->respawn()) {
					it->getBody()->SetTransform(randomSpawn(), 0);
					it->dead = false;
				}
				continue;
			} else alive_people++; // Count alive
			// Unequip power-up if expired
			if (it->powerup.expired()) it->unequip();
			// Check for contacts
			for (b2ContactEdge* ce = it->getBody()->GetContactList(); ce && ce->other; ce = ce->next) {
				ElementType et = NONE;
				if (ce->other->GetUserData()) et = *(static_cast<ElementType*>(ce->other->GetUserData()));
				// Mines
				if (et == MINE) {
					// TODO: Add killer
					kill(&(*it));
					world.DestroyBody(ce->other);
					ce->other = NULL;
				// Ladders
				} else if (et == LADDER)
					it->ladder = Actor::LADDER_YES;
				// Power-ups
				else if (et == POWERUP) {
					ce->other->SetUserData(NULL);
					// Find the powerup
					for (Powerups::iterator pu = powerups.begin(); pu != powerups.end(); ++pu) {
						if (!pu->getBody()->GetUserData()) {
							it->equip(pu->effect);
							world.DestroyBody(ce->other);
							powerups.erase(pu);
							break;
						}
					}
				// Other players
				} else if (et == ACTOR) {
					ce->other->SetUserData(NULL);
					// Find the actor
					for (Actors::iterator ac = actors.begin(); ac != actors.end(); ++ac) {
						if (!ac->getBody()->GetUserData()) {
							it->powerup.touch(&(*it), &(*ac));
							if (it->getY() < ac->getY()) it->airborne = false;
							break;
						}
					}
					ce->other->SetUserData(&ElementTypes[ACTOR]);
				// Ground
				} else if (et == PLATFORM || et == CRATE || et == BRIDGE) {
					if (it->getY() < ce->other->GetPosition().y) it->airborne = false;
					if (et == PLATFORM && it->getY() > ce->other->GetPosition().y - tilesize*0.4f - it->getSize())
						hitwall = true;
				// Border
				} else if (et == BORDER) hitwall = true;
			}
			// Flag tuning
			if (!it->airborne) {
				if (it->ladder == Actor::LADDER_YES) it->ladder = Actor::LADDER_ROOT;
				if (it->doublejump == DJUMP_JUMPED) it->doublejump = DJUMP_ALLOW;
			}
			if (climbing && it->ladder == Actor::LADDER_YES) it->ladder = Actor::LADDER_CLIMBING;
			b2Body* b = it->getBody();
			// Handle wall hit
			if (hitwall && it->ladder != Actor::LADDER_CLIMBING) {
				it->wallpenalty = Countdown(0.25);
				b->GetFixtureList()->SetFriction(0.0);
			} else if (it->wallpenalty() && b->GetFixtureList()->GetFriction() != PLAYER_FRICTION) {
				b->GetFixtureList()->SetFriction(PLAYER_FRICTION);
			}
			// Gravity
			float grav_mult = (it->lograv ? 0.1 : 1.0) * (it->ladder == Actor::LADDER_CLIMBING ? 0.0 : 1.0);
			b->ApplyForceToCenter(b2Vec2(0, b->GetMass() * GRAVITY * grav_mult), true);
			// AI
			if (it->type == Actor::AI) it->brains();
		} //< End of Actors loop
		if (alive_people <= 1) game.noOpponentsLeft();
		// Crates
		for (Crates::iterator it = crates.begin(); it != crates.end(); ++it) {
			b2Body* b = it->getBody();
			float y = b->GetWorldCenter().y - it->getSize()*0.5f;
			// Float on water
			if (y >= h - water_height) {
				b->ApplyForceToCenter(b2Vec2(0, b->GetMass() * GRAVITY * -1.2), true);
				// Water friction
				b->SetLinearVelocity(0.97 * b->GetLinearVelocity());
				b->SetAngularVelocity(0.99 * b->GetAngularVelocity());
			// Respawn if deep
			//} else if (y >= h - water_height*0.333 && is_master) {
				//b->SetLinearVelocity(b2Vec2());
				//b->SetAngularVelocity(0);
				//b->SetTransform(b2Vec2(randf(offset, w-offset), randf(offset, h*0.667)), 0);
			}
			// Gravity
			b->ApplyForceToCenter(b2Vec2(0, b->GetMass() * GRAVITY), true);
		}
		// Remove expired power-ups
		for (Powerups::iterator pu = powerups.begin(); pu != powerups.end(); ) {
			if (pu->expired()) {
				world.DestroyBody(pu->getBody());
				pu = powerups.erase(pu);
				continue;
			}
			++pu;
		}
	} //< Mutex
	// Create power-ups
	if (timer_powerup() && is_master) {
		addPowerup(randf(offset, w-offset), randf(offset, h-offset), game.randPowerup());
		timer_powerup = Countdown(game.getPowerupDelay());
	}
	if (game.roundEnded()) newRound();
	#ifdef USE_THREADS
	// TODO: Hackish
	t = GetSecs() - t;
	int tt = (timeStep - t - 0.001) * 0.5 * 1000;
	if (tt > 0) boost::this_thread::sleep(boost::posix_time::milliseconds(tt));
	#endif
}


std::string World::serialize(bool skip_static) const {
	LOCKMUTEX;
	std::string data = "";
	// Players
	if (!actors.empty()) {
		data += std::string(1, ACTOR);
		data += std::string(1, (char)actors.size());
		for (Actors::const_iterator it = actors.begin(); it != actors.end(); ++it) {
			std::string temp(it->serialize(), sizeof(SerializedEntity));
			data += temp;
		}
	}
	// Crates
	if (!crates.empty()) {
		data += std::string(1, CRATE);
		data += std::string(1, (char)crates.size());
		for (Crates::const_iterator it = crates.begin(); it != crates.end(); ++it) {
			std::string temp(it->serialize(), sizeof(SerializedEntity));
			data += temp;
		}
	}
	// Power-ups
	if (!powerups.empty()) {
		data += std::string(1, POWERUP);
		data += std::string(1, (char)powerups.size());
		for (Powerups::const_iterator it = powerups.begin(); it != powerups.end(); ++it) {
			std::string temp(it->serialize(), sizeof(SerializedEntity));
			data += temp;
		}
	}
	// Static objects
	if (!skip_static) {
		// Platforms
		if (!platforms.empty()) {
			data += std::string(1, PLATFORM);
			data += std::string(1, (char)platforms.size());
			for (Platforms::const_iterator it = platforms.begin(); it != platforms.end(); ++it) {
				std::string temp(it->serialize(), sizeof(SerializedEntity));
				data += temp;
			}
		}
		// Ladders
		if (!ladders.empty()) {
			data += std::string(1, LADDER);
			data += std::string(1, (char)ladders.size());
			for (Ladders::const_iterator it = ladders.begin(); it != ladders.end(); ++it) {
				std::string temp(it->serialize(), sizeof(SerializedEntity));
				data += temp;
			}
		}
		// Bridges
		if (!bridges.empty()) {
			data += std::string(1, BRIDGE);
			data += std::string(1, (char)bridges.size());
			for (Bridges::const_iterator it = bridges.begin(); it != bridges.end(); ++it) {
				std::string temp(it->serialize(), sizeof(SerializedEntity));
				data += temp;
			}
		}
	}
	return data;
}


void World::update(std::string data, Client* client) {
	int pos = 0;
	if (data[pos] == ACTOR) {
		int items = data[pos+1];
		int cnt = 0; pos += 2;
		// New players?
		int oldplayers = actors.size();
		int createnew = items - oldplayers;
		if (createnew > 0) {
			for (int i = 0; i < createnew; ++i) {
				bool me = client && oldplayers + i + 1 == client->getID();
				addActor(10, 10, me ? Actor::HUMAN : Actor::REMOTE, oldplayers+i+1, me ? client : NULL);
			}
		}
		LOCKMUTEX;
		// Update
		for (Actors::iterator it = actors.begin(); it != actors.end() && cnt < items; ++it, ++cnt, pos += sizeof(SerializedEntity)) {
			std::string itemdata(&data[pos], sizeof(SerializedEntity));
			it->unserialize(itemdata);
		}
	}
	if (data[pos] == CRATE) {
		int items = data[pos+1];
		int cnt = 0; pos += 2;
		// Check if we need to create new ones
		int createnew = items - crates.size();
		if (createnew > 0) {
			for (int i = 0; i < createnew; ++i) addCrate(randint(0,w), randint(0,h));
		}
		LOCKMUTEX;
		// Update position etc.
		for (Crates::iterator it = crates.begin(); it != crates.end() && cnt < items; ++it, ++cnt, pos += sizeof(SerializedEntity)) {
			std::string itemdata(&data[pos], sizeof(SerializedEntity));
			it->unserialize(itemdata);
		}
	}
	if (data[pos] == POWERUP) {
		int items = data[pos+1];
		int cnt = 0; pos += 2;
		// Check if we need to crete a delete power-ups
		int createnew = items - powerups.size();
		if (createnew > 0) { // Create new ones
			for (int i = 0; i < createnew; ++i) addPowerup(randint(0,w), randint(0,h),
			  Powerup::PowerupTypes[(int)data[sizeof(SerializedEntity)-2]]);
		} else if (createnew < 0) { // Delete old ones
			LOCKMUTEX;
			for (int i = 0; i < -createnew; ++i) {
				Powerups::iterator it = powerups.end()-1;
				world.DestroyBody(it->getBody());
				powerups.erase(it);
			}
		}
		LOCKMUTEX;
		// Update position etc.
		for (Powerups::iterator it = powerups.begin(); it != powerups.end() && cnt < items; ++it, ++cnt, pos += sizeof(SerializedEntity)) {
			std::string itemdata(&data[pos], sizeof(SerializedEntity));
			it->unserialize(itemdata);
		}
	}
	// Static objects (platforms, ladders...)
	// For now, these are always interpreted as new ones
	if (data[pos] == PLATFORM) {
		int items = data[pos+1];
		pos += 2;
		// Create new
		for (int i = 0; i < items; ++i, pos += sizeof(SerializedEntity)) {
			SerializedEntity* se = reinterpret_cast<SerializedEntity*>(&data[pos]);
			addPlatform(se->x - se->vx / 2 * tilesize, se->y - tilesize*0.5f, se->vx, true);
		}
	}
	if (data[pos] == LADDER) {
		int items = data[pos+1];
		pos += 2;
		// Create new
		for (int i = 0; i < items; ++i, pos += sizeof(SerializedEntity)) {
			SerializedEntity* se = reinterpret_cast<SerializedEntity*>(&data[pos]);
			addLadder(se->x - tilesize*0.5f, se->y - se->vy / 2 * tilesize, se->vy);
		}
	}
	if (data[pos] == BRIDGE) {
		int items = data[pos+1];
		pos += 2;
		// Create new
		for (int i = 0; i < items; ++i, pos += sizeof(SerializedEntity)) {
			SerializedEntity* se = reinterpret_cast<SerializedEntity*>(&data[pos]);
			addBridge(se->id, se->type);
		}
	}
}


void World::updateViewport() {
	// Magick zooming camera variables
	static const float xmargin = 8.0;
	static const float ymargin = 4.0f;
	static const float lerp_speed = 0.03;
	float x1 = w, y1 = h, x2 = 0, y2 = 0;
	float ar = w / h;
	{
		LOCKMUTEX;
		for (Actors::const_iterator it = actors.begin(); it != actors.end(); ++it) {
			// Calculate viewport borders
			b2Vec2 itpos = it->getBody()->GetWorldCenter();
			if (itpos.x < x1) x1 = itpos.x;
			if (itpos.x > x2) x2 = itpos.x;
			if (itpos.y < y1) y1 = itpos.y;
			if (itpos.y > y2) y2 = itpos.y;
		}
	}
	// Add margins and clamp box to world
	x1 -= xmargin; x2 += xmargin;
	y1 -= ymargin; y2 += ymargin;
	if (x2 - x1 >= w) { x1 = 0; x2 = w; }
	if (y2 - y1 >= h) { y1 = 0; y2 = h; }
	// Correct aspect ratio
	float boxw = (x2-x1), boxh = (y2-y1);
	if (boxh > boxw / ar) boxw = boxh * ar;
	else boxh = boxw / ar;
	float midx = (x1+x2)*0.5f;
	float midy = (y1+y2)*0.5f;
	x1 = midx-boxw*0.5f;
	x2 = midx+boxw*0.5f;
	y1 = midy-boxh*0.5f;
	y2 = midy+boxh*0.5f;
	// Move back inside screen
	float xcorr = 0, ycorr = 0;
	if (x1 < 0) xcorr = -x1;
	if (x2 > w) xcorr = w-x2;
	if (y1 < 0) ycorr = -y1;
	if (y2 > h) ycorr = h-y2;
	x1 += xcorr; x2 += xcorr;
	y1 += ycorr; y2 += ycorr;
	// Interplate smoothly to the new viewport
	x1 = lerp(view_topleft.x, x1, lerp_speed);
	y1 = lerp(view_topleft.y, y1, lerp_speed);
	x2 = lerp(view_bottomright.x, x2, lerp_speed);
	y2 = lerp(view_bottomright.y, y2, lerp_speed);
	{
		LOCKMUTEX;
		// Set the coords
		view_topleft.x     = x1; view_topleft.y     = y1;
		view_bottomright.x = x2; view_bottomright.y = y2;
	}
}


void World::draw() const {
	{ // Magic zooming viewport
		LOCKMUTEX;
		glMatrixMode(GL_PROJECTION);
			glPushMatrix();
			glLoadIdentity();
			gluOrtho2D(view_topleft.x, view_bottomright.x, view_bottomright.y, view_topleft.y);
		glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
	}
	{ // Background
		static const int texsize = 8;
		CoordArray v_arr, t_arr;
		for (int j = 0; j < h/texsize + 1; j++) {
			for (int i = 0; i < w/texsize + 1; i++) {
				float xx = i * texsize;
				float yy = j * texsize;
				float verts[] = { xx, yy + texsize,
								  xx, yy,
								  xx + texsize, yy,
								  xx + texsize, yy + texsize };
				v_arr.insert(v_arr.end(), &verts[0], &verts[8]);
				t_arr.insert(t_arr.end(), &tex_square[0], &tex_square[8]);
			}
		}
		LOCKMUTEX;
		drawVertexArray(&v_arr[0], &t_arr[0], v_arr.size()/2, texture_background);
	}
	{
		LOCKMUTEX;
		// Ladders
		for (Ladders::const_iterator it = ladders.begin(); it != ladders.end(); ++it) {
			it->draw();
		}
		// Platforms
		for (Platforms::const_iterator it = platforms.begin(); it != platforms.end(); ++it) {
			it->draw();
		}
		// Bridges
		for (Bridges::const_iterator it = bridges.begin(); it != bridges.end(); ++it) {
			it->draw();
		}
		// Crates
		for (Crates::const_iterator it = crates.begin(); it != crates.end(); ++it) {
			it->draw();
		}
		// Players
		for (Actors::const_iterator it = actors.begin(); it != actors.end(); ++it) {
			if (!it->is_dead() && !it->invisible) it->draw();
		}
		// Power-ups
		for (Powerups::const_iterator it = powerups.begin(); it != powerups.end(); ++it) {
			it->draw();
		}
	}
	{ // Water
		CoordArray v_arr, t_arr;
		for (int i = 0; i < w / water_height + 1; i++) {
			float xx = i * water_height;
			float yy = h - water_height;
			float verts[] = { xx, yy + water_height,
							  xx, yy,
							  xx + water_height, yy,
							  xx + water_height, yy + water_height };
			v_arr.insert(v_arr.end(), &verts[0], &verts[8]);
			t_arr.insert(t_arr.end(), &tex_square[0], &tex_square[8]);
		}
		LOCKMUTEX;
		drawVertexArray(&v_arr[0], &t_arr[0], v_arr.size()/2, texture_water);
	}
	glMatrixMode(GL_PROJECTION);
		glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
}
