/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include "looper-wall.h"
#include <game/server/entities/character.h>
#include <game/server/player.h>
#include <infcroya/croyaplayer.h>
#include "engine/shared/config.h"

const float g_BarrierMaxLength = 400.0;
const float g_BarrierRadius = 0.0;

CLooperWall::CLooperWall(CGameWorld *pGameWorld, vec2 Pos1, vec2 Pos2, int Owner)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_LOOPER_WALL, Pos1)
{
	m_Pos = Pos1;
	if(distance(Pos1, Pos2) > g_BarrierMaxLength)
	{
		m_Pos2 = Pos1 + normalize(Pos2 - Pos1)*g_BarrierMaxLength;
	}
	else m_Pos2 = Pos2;
	m_Owner = Owner;
	m_LifeSpan = Server()->TickSpeed()*g_Config.m_InfBarrierLifeSpan;
	GameWorld()->InsertEntity(this);
	m_EndPointID = Server()->SnapNewID();
	m_EndPointID2 = Server()->SnapNewID();
	m_WallFlashTicks = 0;
}

CLooperWall::~CLooperWall()
{
	Server()->SnapFreeID(m_EndPointID);
	Server()->SnapFreeID(m_EndPointID2);
}

void CLooperWall::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CLooperWall::Tick()
{
	if(IsMarkedForDestroy()) return;

	if (m_WallFlashTicks > 0) 
		m_WallFlashTicks--;

	m_LifeSpan--;
	
	if(m_LifeSpan < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
	}
	else
	{
		// Find other players
		for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			if(p->IsHuman()) continue;

			vec2 IntersectPos = closest_point_on_line(m_Pos, m_Pos2, p->GetPos());
			float Len = distance(p->GetPos(), IntersectPos);
			if(Len < p->GetProximityRadius()+g_BarrierRadius)
			{
				if(p->GetPlayer())
				{
					for(CCharacter *pHook = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHook; pHook = (CCharacter *)pHook->TypeNext())
					{
						
						//skip classes that can't die.
						//if(p->GetClass() == PLAYERCLASS_UNDEAD && p->IsFrozen()) continue;
						//if(p->GetClass() == PLAYERCLASS_VOODOO && p->m_VoodooAboutToDie) continue;
						
						if(
							pHook->GetPlayer() &&
							pHook->IsHuman() &&
							pHook->GetCharacterCore().m_HookedPlayer == p->GetPlayer()->GetCID() &&
							pHook->GetPlayer()->GetCID() != m_Owner //The engineer will get the point when the infected dies
							//&& p->m_LastFreezer != pHook->GetPlayer()->GetCID() //The ninja will get the point when the infected dies
						)
						{
							//int ClientID = pHook->GetPlayer()->GetCID();
							//Server()->RoundStatistics()->OnScoreEvent(ClientID, SCOREEVENT_HELP_HOOK_BARRIER, pHook->GetClass(), Server()->ClientName(ClientID), GameServer()->Console());
							//GameServer()->SendScoreSound(pHook->GetPlayer()->GetCID());
						}
					}
					
					// int LifeSpanReducer = ((Server()->TickSpeed()*g_Config.m_InfBarrierTimeReduce)/100);
					// m_WallFlashTicks = 10;
					
					// m_LifeSpan -= LifeSpanReducer;
				}
				
				if (!p->IsInSlowMotion())
				{
					p->SlowMotionEffect(g_Config.m_InfSlowMotionWallDuration);
				}
/* 				CPlayer* pKiller = GameServer()->m_apPlayers[m_Owner];
				if (p->GetPlayer()->GetCID() != pKiller->GetCID())
					pKiller->GetCroyaPlayer()->OnKill(p->GetPlayer()->GetCID()); */
			}
		}
	}
}

void CLooperWall::TickPaused()
{
	//~ ++m_EvalTick;
}

void CLooperWall::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	// Laser dieing animation
	int LifeDiff = 0;
	if (m_WallFlashTicks > 0) // flash laser for a few ticks when zombie jumps
		LifeDiff = 5;
	else if (m_LifeSpan < 1*Server()->TickSpeed())
		LifeDiff = random_int_range(4, 5);
	else if (m_LifeSpan < 2*Server()->TickSpeed())
		LifeDiff = random_int_range(3, 5);
	else if (m_LifeSpan < 3*Server()->TickSpeed())
		LifeDiff = random_int_range(2, 4);
	else if (m_LifeSpan < 4*Server()->TickSpeed())
		LifeDiff = random_int_range(1, 3);
	else if (m_LifeSpan < 5*Server()->TickSpeed())
		LifeDiff = random_int_range(0, 2);
	else if (m_LifeSpan < 6*Server()->TickSpeed())
		LifeDiff = random_int_range(0, 1);
	else if (m_LifeSpan < 7*Server()->TickSpeed())
		LifeDiff = (random_prob(3.0f/4.0f)) ? 1 : 0;
	else if (m_LifeSpan < 8*Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f/6.0f)) ? 1 : 0;
	else if (m_LifeSpan < 9*Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f/6.0f)) ? 0 : -1;
	else if (m_LifeSpan < 10*Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f/6.0f)) ? 0 : -1;
	else if (m_LifeSpan < 11*Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f/6.0f)) ? -1 : -Server()->TickSpeed()*2;
	else
		LifeDiff = -Server()->TickSpeed()*2;
	
	{
		int THICKNESS=10.0f;
		vec2 dirVec = vec2(m_Pos.x-m_Pos2.x, m_Pos.y-m_Pos2.y);
		vec2 dirVecN = normalize(dirVec);
		vec2 dirVecT = vec2(dirVecN.y*THICKNESS, -dirVecN.x*THICKNESS);

		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		vec2 Pos1start = m_Pos - dirVecT;
		vec2 Pos1end = m_Pos2 - dirVecT;

		pObj->m_X = (int)Pos1start.x;
		pObj->m_Y = (int)Pos1start.y;
		pObj->m_FromX = (int)Pos1end.x;
		pObj->m_FromY = (int)Pos1end.y;
		pObj->m_StartTick = Server()->Tick()-LifeDiff;


		vec2 Pos2start = m_Pos + dirVecT;
		vec2 Pos2end = m_Pos2 + dirVecT;

		CNetObj_Laser *pObj2 = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_EndPointID2, sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj2->m_X = (int)Pos2start.x;
		pObj2->m_Y = (int)Pos2start.y;
		pObj2->m_FromX = (int)Pos2end.x;
		pObj2->m_FromY = (int)Pos2end.y;
		pObj2->m_StartTick = Server()->Tick()-LifeDiff;
	}
	// if(!Server()->GetClientAntiPing(SnappingClient))
	// CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_EndPointID, sizeof(CNetObj_Laser)));
	// if(!pObj)
	// 	return;
		
	// vec2 Pos = m_Pos2;

	// pObj->m_X = (int)Pos.x - 3;
	// pObj->m_Y = (int)Pos.y - 3;
	// pObj->m_FromX = (int)m_Pos2.x - 3;
	// pObj->m_FromY = (int)m_Pos2.y - 3;
	// pObj->m_StartTick = Server()->Tick();

	// CNetObj_Laser *pObj2 = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_EndPointID2, sizeof(CNetObj_Laser)));
	// if(!pObj2)
	// 	return;
		
	// pObj2->m_X = (int)Pos.x + 3;
	// pObj2->m_Y = (int)Pos.y + 3;
	// pObj2->m_FromX = (int)m_Pos2.x + 3;
	// pObj2->m_FromY = (int)m_Pos2.y + 3;
	// pObj2->m_StartTick = Server()->Tick();
}
