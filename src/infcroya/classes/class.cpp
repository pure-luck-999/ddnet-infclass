#include "class.h"
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <infcroya/croyaplayer.h>
#include <game/server/player.h>
#include <game/server/entities/projectile.h>
#include <generated/server_data.h>


IClass::~IClass()
{
}

void IClass::Tick(CCharacter* pChr)
{
}

void IClass::GrenadeShoot(CCharacter* pChr, vec2 ProjStartPos, vec2 Direction) {
	int ClientID = pChr->GetPlayer()->GetCID();
	CGameContext* pGameServer = pChr->GameServer();
	CGameWorld* pGameWorld = pChr->GameWorld();

	new CProjectile(pGameWorld, WEAPON_GRENADE,
		ClientID,
		ProjStartPos,
		Direction,
		(int)(pChr->Server()->TickSpeed() * pGameServer->Tuning()->m_GrenadeLifetime),
		g_pData->m_Weapons.m_Grenade.m_pBase->m_Damage, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

		pGameServer->CreateSound(pChr->GetPos(), SOUND_GRENADE_FIRE);
}

bool IClass::WillItFire(vec2 Direction, vec2 ProjStartPos, int Weapon, CCharacter* pChr) {
	return true;
}

void IClass::UnlockPosition(CCharacter* pChr) {
}

void IClass::ItDoubleJumps(CCharacter* pChr) {
	//Double jumps
	CroyaPlayer* cp = pChr->GetCroyaPlayer();
	if (pChr->IsGrounded()) cp->SetAirJumpCounter(0);
	if (pChr->GetCharacterCore().m_TriggeredEvents & COREEVENTFLAG_AIR_JUMP && cp->GetAirJumpCounter() < 1)
	{
		pChr->GetCharacterCore().m_Jumped &= ~2;
		cp->SetAirJumpCounter(cp->GetAirJumpCounter() + 1);
	}
}

void IClass::HammerShoot(CCharacter* pChr, vec2 ProjStartPos) {
	int ClientID = pChr->GetPlayer()->GetCID();
	CGameContext* pGameServer = pChr->GameServer();

	// reset objects Hit
	pChr->SetNumObjectsHit(0);
	pGameServer->CreateSound(pChr->GetPos(), SOUND_HAMMER_FIRE);

	CCharacter *apEnts[MAX_CLIENTS];
	int Hits = 0;
	int Num = pChr->GameServer()->m_World.FindEntities(ProjStartPos, pChr->GetProximityRadius() * 0.5f, (CEntity **)apEnts,
													   MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

	for (int i = 0; i < Num; ++i)
	{
		CCharacter *pTarget = apEnts[i];

		if ((pTarget == pChr) || pGameServer->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), NULL, NULL))
			continue;

		// set his velocity to fast upward (for now)
		if (length(pTarget->GetPos() - ProjStartPos) > 0.0f)
			pGameServer->CreateHammerHit(pTarget->GetPos() - normalize(pTarget->GetPos() - ProjStartPos) * pChr->GetProximityRadius() * 0.5f);
		else
			pGameServer->CreateHammerHit(ProjStartPos);

		vec2 Dir;
		if (length(pTarget->GetPos() - pChr->GetPos()) > 0.0f)
			Dir = normalize(pTarget->GetPos() - pChr->GetPos());
		else
			Dir = vec2(0.f, -1.f);

		int DAMAGE = 20; // should kill
		bool ShouldHit = false;
		bool ShouldHeal = false;
		bool ShouldInfect = false;

		if (pChr->IsZombie() && pTarget->IsHuman()) {
			ShouldInfect = true;
		}

		if (pChr->IsZombie() && pTarget->IsZombie() && pTarget->GetHealthArmorSum() < 20) {
			ShouldHeal = true;
		}

		if (pChr->IsHuman() && pTarget->IsZombie()) {
			ShouldHit = true;
		}

		if (pChr->GetCroyaPlayer()->GetClassNum() == Class::BAT && ShouldInfect) {
			ShouldInfect = false;
			ShouldHit = true;
			DAMAGE = 1;
		}

		if (pChr->GetCroyaPlayer()->GetClassNum() == Class::WORKER && ShouldInfect) {
			ShouldInfect = false;
			ShouldHit = true;
			DAMAGE = 2;
		}

		if (ShouldHit)
		{
			pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, Dir * -1, DAMAGE,
								ClientID, pChr->GetActiveWeapon());
		}

		if (ShouldInfect) {
			pTarget->Infect(ClientID);
		}

		if (ShouldHeal) {
			pTarget->IncreaseOverallHp(4);
			pChr->IncreaseOverallHp(1);
			pTarget->SetEmote(EMOTE_HAPPY, pChr->Server()->Tick() + pChr->Server()->TickSpeed());
		}

	}

	// if we Hit anything, we have to wait for the reload
	if (Hits)
		pChr->SetReloadTimer(pChr->Server()->TickSpeed() / 3);
}

void IClass::OnMouseWheelDown()
{
}

void IClass::OnMouseWheelUp()
{
}

void IClass::OnCharacterSpawn(CCharacter* pChr)
{
	pChr->SetInfected(IsInfectedClass());
	pChr->ResetWeaponsHealth();
	InitialWeaponsHealth(pChr);
}

int IClass::OnCharacterDeath(CCharacter* pVictim, CPlayer* pKiller, int Weapon)
{
	return 0;
}

const CSkin& IClass::GetSkin() const
{
	return m_Skin;
}

void IClass::SetSkin(CSkin skin)
{
	m_Skin = skin;
}

bool IClass::IsInfectedClass() const
{
	return m_Infected;
}

void IClass::SetInfectedClass(bool Infected)
{
	m_Infected = Infected;
}

std::string IClass::GetName() const
{
	return m_Name;
}

void IClass::SetName(std::string Name)
{
	m_Name = Name;
}
