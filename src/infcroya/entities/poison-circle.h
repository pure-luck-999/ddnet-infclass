/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_POISON_CIRCLE_H
#define GAME_SERVER_ENTITIES_POISON_CIRCLE_H

#include <game/server/entity.h>

class CPoisonCircle : public CEntity
{
public:
	enum
	{
		NUM_SIDE = 12,
		NUM_PARTICLES = 12,
		NUM_IDS = NUM_SIDE + NUM_PARTICLES,
	};
	
public:
	CPoisonCircle(CGameWorld *pGameWorld, vec2 Pos, int Owner);
	virtual ~CPoisonCircle();

	virtual void Snap(int SnappingClient);
	virtual void Reset();
	virtual void TickPaused();
	virtual void Tick();

	int GetOwner() const;

private:
	int m_IDs[NUM_IDS];
	
public:
	int m_StartTick;
	float m_DetectionRadius;
	int m_Owner;
};

#endif
