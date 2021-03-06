/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <antibot/antibot_data.h>
#include <engine/shared/config.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>
#include <new>

#include "character.h"
#include "laser.h"
#include <game/server/entities/projectile.h>

// INFCROYA BEGIN ------------------------------------------------------------
#include <infcroya/classes/class.h>
#include <game/server/eventhandler.h>
#include <infcroya/entities/engineer-wall.h>
#include <infcroya/entities/looper-wall.h>
#include <infcroya/entities/biologist-mine.h>
#include <infcroya/entities/soldier-bomb.h>
#include <infcroya/entities/scientist-mine.h>
#include <infcroya/entities/medic-grenade.h>
#include <infcroya/entities/merc-bomb.h>
#include <infcroya/entities/scatter-grenade.h>
// INFCROYA END ------------------------------------------------------------//

#include "light.h"
#include <game/server/gamemodes/DDRace.h>
#include <game/server/gamemodes/mod.h>
#include <game/server/score.h>

MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld) :
	CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 10;
	m_Immunity = 4;
	m_Armor = 0;
	m_StrongWeakID = 0;
	m_IsBot = false;
	m_BotClientID = -1;
	m_IsInSlowMotion = false;

	// INFCROYA BEGIN ------------------------------------------------------------
	m_Infected = false;
	m_HeartID = Server()->SnapNewID();
	m_TaxiID = Server()->SnapNewID();
	m_HookProtID = Server()->SnapNewID();
	m_FreeTaxi = false;
	m_TaxiPassenger = false;
	m_FirstShot = true;
	m_BarrierHintID = Server()->SnapNewID();
	m_BarrierHintIDs.set_size(2);
	m_Core.m_CannotHookNow = false;
	for (int i = 0; i < 2; i++)
	{
		m_BarrierHintIDs[i] = Server()->SnapNewID();
	}
	m_RespawnPointID = Server()->SnapNewID();
	
	m_IsStunned = false;
	m_StunTime = -1;
	//m_IsFrozen = false;
	//m_FrozenTime = -1;
	//m_PoisonTick = 0;
	m_InAirTick = 0;
	//m_HookProtected = true; // will be updated from CPlayer
	// INFCROYA END ------------------------------------------------------------//
}

void CCharacter::Reset()
{
	DestroyChildEntities();
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_Core.m_CannotHookNow = false;
	m_SlowMotionTick = -1;
	m_IsInSlowMotion = false;
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_LastNoAttachSound = -1;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;
	m_LastRefillJumps = false;
	m_LastPenalty = false;
	m_LastBonus = false;

	m_TeleGunTeleport = false;
	m_IsBlueTeleGunTeleport = false;
	m_Solo = false;

	m_pPlayer = pPlayer;
	m_Pos = Pos;

	mem_zero(&m_LatestPrevPrevInput, sizeof(m_LatestPrevPrevInput));
	m_SpawnTick = Server()->Tick();
	m_WeaponChangeTick = Server()->Tick();
	Antibot()->OnSpawn(m_pPlayer->GetCID());

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision(), &((CGameControllerDDRace *)GameServer()->m_pController)->m_Teams.m_Core, &((CGameControllerDDRace *)GameServer()->m_pController)->m_TeleOuts);
	m_Core.m_ActiveWeapon = WEAPON_GUN;
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	Teams()->OnCharacterSpawn(GetPlayer()->GetCID());

	DDRaceInit();

	m_TuneZone = GameServer()->Collision()->IsTune(GameServer()->Collision()->GetMapIndex(Pos));
	m_TuneZoneOld = -1; // no zone leave msg on spawn
	m_NeededFaketuning = 0; // reset fake tunings on respawn and send the client
	SendZoneMsgs(); // we want a entermessage also on spawn
	GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone);

	Server()->StartRecord(m_pPlayer->GetCID());
	if (GetCroyaPlayer())
		GetCroyaPlayer()->AntigravityOff();

	return true;
}

void CCharacter::Destroy()
{
	//DestroyChildEntities();
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
	m_Solo = false;
	// INFCROYA BEGIN ------------------------------------------------------------
	if (m_HeartID >= 0) {
		Server()->SnapFreeID(m_HeartID);
		m_HeartID = -1;
	}
	if (m_HookProtID >= 0) {
		Server()->SnapFreeID(m_HookProtID);
		m_HookProtID = -1;
	}
	if (m_BarrierHintID >= 0) {
		Server()->SnapFreeID(m_BarrierHintID);
		m_BarrierHintID = -1;
	}
	if (m_RespawnPointID >= 0) {
		Server()->SnapFreeID(m_RespawnPointID);
		m_RespawnPointID = -1;
	}

	if (m_BarrierHintIDs[0] >= 0) {
		for (int i = 0; i < 2; i++) {
			Server()->SnapFreeID(m_BarrierHintIDs[i]);
			m_BarrierHintIDs[i] = -1;
		}
	}
	if (m_TaxiID >= 0) {
		Server()->SnapFreeID(m_TaxiID);
		m_TaxiID = -1;
		m_FreeTaxi = false;
		m_TaxiPassenger = false;
	}
	// INFCROYA END ------------------------------------------------------------//
}

void CCharacter::SetWeapon(int W)
{
	if (!GameWorld())
		return;
	if(W == m_Core.m_ActiveWeapon)
		return;

	m_LastWeapon = m_Core.m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_Core.m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));

	if(m_Core.m_ActiveWeapon < 0 || m_Core.m_ActiveWeapon >= NUM_WEAPONS)
		m_Core.m_ActiveWeapon = 0;
}

void CCharacter::SetSolo(bool Solo)
{
	m_Solo = Solo;
	m_Core.m_Solo = Solo;
	Teams()->m_Core.SetSolo(m_pPlayer->GetCID(), Solo);

	if(Solo)
		m_NeededFaketuning |= FAKETUNE_SOLO;
	else
		m_NeededFaketuning &= ~FAKETUNE_SOLO;

	GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x + m_ProximityRadius / 2, m_Pos.y + m_ProximityRadius / 2 + 5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x - m_ProximityRadius / 2, m_Pos.y + m_ProximityRadius / 2 + 5))
		return true;

	int MoveRestrictionsBelow = GameServer()->Collision()->GetMoveRestrictions(m_Pos + vec2(0, m_ProximityRadius / 2 + 4), 0.0f);
	if(MoveRestrictionsBelow & CANTMOVE_DOWN)
	{
		return true;
	}

	return false;
}

void CCharacter::HandleJetpack()
{
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_Core.m_ActiveWeapon == WEAPON_GRENADE || m_Core.m_ActiveWeapon == WEAPON_SHOTGUN || m_Core.m_ActiveWeapon == WEAPON_LASER)
		FullAuto = true;
	//if (m_Jetpack && m_Core.m_ActiveWeapon == WEAPON_GUN)
	if (m_Core.m_ActiveWeapon == WEAPON_GUN)
		FullAuto = true;

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire & 1) && m_aWeapons[m_Core.m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_Core.m_ActiveWeapon].m_Ammo || m_FreezeTime)
		return;

	switch(m_Core.m_ActiveWeapon)
	{
	case WEAPON_GUN:
	{
		if(m_Jetpack)
		{
			if (m_Jetpack)
			{
				float Strength;
				if (!m_TuneZone)
					Strength = GameServer()->Tuning()->m_JetpackStrength;
				else
					Strength = GameServer()->TuningList()[m_TuneZone].m_JetpackStrength;
				TakeDamageDDNet(Direction * -1.0f * (Strength / 100.0f / 6.11f), 0, m_pPlayer->GetCID(), m_Core.m_ActiveWeapon);
			}
		}
	}
	}
}

void CCharacter::HandleNinja()
{
	if(m_Core.m_ActiveWeapon != WEAPON_NINJA)
		return;

	if((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		RemoveNinja();

		return;
	}

    if (m_IsStunned) {
		if ((Server()->Tick() > m_StunTime)) {
			m_Core.m_CannotHookNow = false;
			Unstun();
			return;
		}
	} 

	if (Stunned()) {
		m_Core.m_CannotHookNow = true;
		m_Core.m_Vel = vec2(0.f, 0.f);
		return;
	}

	int NinjaTime = m_Ninja.m_ActivationTick + (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000) - Server()->Tick();

/* 	if (NinjaTime % Server()->TickSpeed() == 0 && NinjaTime / Server()->TickSpeed() <= 5)
	{
		GameServer()->CreateDamageInd(m_Pos, 0, NinjaTime / Server()->TickSpeed(), Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
	} */

	//m_Armor = clamp(10 - (NinjaTime / 15), 0, 10);

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;
	if (IsGrounded())
		m_Ninja.m_ActivationTick = Server()->Tick();

	if(m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * m_Ninja.m_OldVelAmount;
	}

	if(m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity **)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			// check that we're not in solo part
			if(Teams()->m_Core.GetSolo(m_pPlayer->GetCID()))
				return;

			for(int i = 0; i < Num; ++i)
			{
				if(aEnts[i] == this)
					continue;

				// Don't hit players in other teams
				if(Team() != aEnts[i]->Team())
					continue;

				// Don't hit players in solo parts
				if(Teams()->m_Core.GetSolo(aEnts[i]->m_pPlayer->GetCID()))
					return;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for(int j = 0; j < m_NumObjectsHit; j++)
				{
					if(m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if(bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if(distance(aEnts[i]->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

				aEnts[i]->TakeDamageDDNet(vec2(0, -10.0f), g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA);
			}
		}

		return;
	}

	return;
}

void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got || !m_aWeapons[m_QueuedWeapon].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_Core.m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	bool Anything = false;
	for(int i = 0; i < NUM_WEAPONS - 1; ++i)
		 if(m_aWeapons[i].m_Got)
			 Anything = true;
	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128 && Next > 0) // make sure we only try sane stuff
	{
		if (!GetCroyaPlayer())
			return;
		GetCroyaPlayer()->OnMouseWheelDown(this);
		if (Anything) {
			while(Next) // Next Weapon selection
			{
				WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
				if(m_aWeapons[WantedWeapon].m_Got) {
					Next--;
				}
			}

		}
	}

	if(Prev < 128 && Prev > 0) // make sure we only try sane stuff
	{
		if (!GetCroyaPlayer())
			return;
		GetCroyaPlayer()->OnMouseWheelUp(this);
		if (Anything) {
			while(Prev) // Prev Weapon selection
			{
				WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
				if(m_aWeapons[WantedWeapon].m_Got) {
					Prev--;
				}
			}
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon - 1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_Core.m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	if(GetCroyaPlayer()->GetClassNum() == Class::NINJA && WantedWeapon != WEAPON_NINJA)
	  WantedWeapon = WEAPON_HAMMER;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if (m_IsBot) {
		SetWeapon(WEAPON_HAMMER);
	}
	if(m_ReloadTimer != 0)
	{
		if(m_LatestInput.m_Fire & 1)
		{
			Antibot()->OnHammerFireReloading(m_pPlayer->GetCID());
		}
		return;
	}

	DoWeaponSwitch();
	if (m_IsBot) {
		m_LatestInput.m_TargetX = m_Core.m_Input.m_TargetX;
		m_LatestInput.m_TargetY = m_Core.m_Input.m_TargetY;
	}
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_Core.m_ActiveWeapon == WEAPON_GRENADE || m_Core.m_ActiveWeapon == WEAPON_SHOTGUN || m_Core.m_ActiveWeapon == WEAPON_LASER)
		FullAuto = true;
	//if(m_Jetpack && m_Core.m_ActiveWeapon == WEAPON_GUN)
	if(m_Core.m_ActiveWeapon == WEAPON_HAMMER && m_IsBot)
		FullAuto = true;
	if(m_Core.m_ActiveWeapon == WEAPON_GUN)
		FullAuto = true;
	// allow firing directly after coming out of freeze or being unfrozen
	// by something
	if(m_FrozenLastTick)
		FullAuto = true;

	// don't fire hammer when player is deep and sv_deepfly is disabled
	if(!g_Config.m_SvDeepfly && m_Core.m_ActiveWeapon == WEAPON_HAMMER && m_DeepFreeze)
		return;

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire & 1) && m_aWeapons[m_Core.m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(m_IsBot && m_Core.m_Direction !=0 )
		WillFire = true;

	if(!WillFire)
		return;

	if(m_FreezeTime)
	{
		// Timer stuff to avoid shrieking orchestra caused by unfreeze-plasma
		if(m_PainSoundTimer <= 0 && !(m_LatestPrevInput.m_Fire & 1))
		{
			m_PainSoundTimer = 1 * Server()->TickSpeed();
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		}
		return;
	}

	// check for ammo
	if(!m_aWeapons[m_Core.m_ActiveWeapon].m_Ammo)
	{
		/*// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);*/
		return;
	}

	vec2 ProjStartPos = m_Pos + Direction * m_ProximityRadius * 0.75f;

	if (!m_pCroyaPlayer || !m_pCroyaPlayer->WillItFire(Direction, ProjStartPos, m_Core.m_ActiveWeapon, this)) {
		if (GetLastNoAttachSound() + Server()->TickSpeed() * 0.5 <= Server()->Tick())
		{
			GameServer()->CreateSound(GetPos(), SOUND_WEAPON_NOAMMO);
			SetLastNoAttachSound(Server()->Tick());
		}
		return;
	}

	switch(m_Core.m_ActiveWeapon)
	{
		case WEAPON_HAMMER:
		{
			m_pCroyaPlayer->OnWeaponFire(Direction, ProjStartPos, WEAPON_HAMMER, this);

		} break;

		case WEAPON_GUN:
		{
			if(!m_Jetpack || !m_pPlayer->m_NinjaJetpack)
			{
				m_pCroyaPlayer->OnWeaponFire(Direction, ProjStartPos, WEAPON_GUN, this);
			}
		} break;

		case WEAPON_SHOTGUN:
		{
			m_pCroyaPlayer->OnWeaponFire(Direction, ProjStartPos, WEAPON_SHOTGUN, this);
		} break;

		case WEAPON_GRENADE:
		{
			m_pCroyaPlayer->OnWeaponFire(Direction, ProjStartPos, WEAPON_GRENADE, this);
		} break;

		case WEAPON_LASER:
		{
			m_pCroyaPlayer->OnWeaponFire(Direction, ProjStartPos, WEAPON_LASER, this);
		} break;

		case WEAPON_NINJA:
		{
			if (Stunned())
				return;

			// reset Hit objects
			m_NumObjectsHit = 0;
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_HookPos = m_Core.m_Pos;
			int ClientID = GetPlayer()->GetCID();
			for(int i = 0; i < MAX_CLIENTS; i++) // linear search, better would be a stored attribute of hooker
			{
				CCharacterCore *pCharCore = m_Core.m_pWorld->m_apCharacters[i];
				if (!pCharCore || pCharCore == &m_Core)
					continue;
				if (pCharCore->m_HookedPlayer == -1 || pCharCore->m_HookedPlayer != ClientID)
				    continue;
				pCharCore->m_HookedPlayer = -1;
				pCharCore->m_HookState = HOOK_RETRACTED;
				pCharCore->m_HookPos = m_Core.m_Pos;
			}

			m_Ninja.m_ActivationDir = Direction;
			m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);
			m_Ninja.m_ActivationTick -= (Server()->TickSpeed() * 5);

		m_Ninja.m_ActivationDir = Direction;
		m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
		m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);

		GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
	}
	break;
	}

	m_AttackTick = Server()->Tick();

	if(m_aWeapons[m_Core.m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_Core.m_ActiveWeapon].m_Ammo--;

	m_ReloadTimer = Server()->GetFireDelay(GetInfWeaponID(m_Core.m_ActiveWeapon)) * Server()->TickSpeed() / 1000;

	if(!m_ReloadTimer)
	{
		float FireDelay;
		if(!m_TuneZone)
			GameServer()->Tuning()->Get(38 + m_Core.m_ActiveWeapon, &FireDelay);
		else
			GameServer()->TuningList()[m_TuneZone].Get(38 + m_Core.m_ActiveWeapon, &FireDelay);
		m_ReloadTimer = FireDelay * Server()->TickSpeed() / 1000;
	}
}

void CCharacter::HandleWeapons()
{
	//ninja
	HandleNinja();
	HandleJetpack();

	if(m_PainSoundTimer > 0)
		m_PainSoundTimer--;

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();
	// ammo regen
	for(int Weapon = 0; Weapon < WEAPON_NINJA; Weapon++) {
/* 		if(!m_aWeapons[m_QueuedWeapon].m_Got)
		  continue; */
		int InfWID = GetInfWeaponID(Weapon);
		int MaxAmmo = Server()->GetMaxAmmo(InfWID);
		int AmmoRegenTime = Server()->GetAmmoRegenTime(InfWID);

		if (InfWID == INFWEAPON_MERCENARY_GUN)
		{
			if (m_InAirTick > Server()->TickSpeed() * 4)
			{
				AmmoRegenTime = 0;
			}
		}

		if(AmmoRegenTime)
		{
			// If equipped and not active, regen ammo?
			if (m_ReloadTimer <= 0)
			{
				if (m_aWeapons[Weapon].m_AmmoRegenStart < 0)
					m_aWeapons[Weapon].m_AmmoRegenStart = Server()->Tick();

				if ((Server()->Tick() - m_aWeapons[Weapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
				{
					// Add some ammo
					m_aWeapons[Weapon].m_Ammo = minimum(m_aWeapons[Weapon].m_Ammo + 1, MaxAmmo);
					m_aWeapons[Weapon].m_AmmoRegenStart = -1;
				}
			}
			else
			{
				m_aWeapons[Weapon].m_AmmoRegenStart = -1;
			}
		}
	}
	if (!IsGrounded() && (m_Core.m_HookState != HOOK_GRABBED || m_Core.m_HookedPlayer != -1))
		m_InAirTick++;
	else
		m_InAirTick = 0;

	return;
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if(m_Core.m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_Core.m_ActiveWeapon;
	m_Core.m_ActiveWeapon = WEAPON_NINJA;

	if(!m_aWeapons[WEAPON_NINJA].m_Got)
		GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
}

void CCharacter::Unstun()
{
	if (!Stunned()) {
		return;
	}
	RemoveNinja();
	m_IsStunned = false;
}

void CCharacter::GiveNinjaIfGrounded() {
	if (IsGrounded())
	  GiveNinja();
}

void CCharacter::RemoveNinja()
{
	if (m_Core.m_ActiveWeapon == WEAPON_NINJA) {
		m_Ninja.m_CurrentMoveTime = 0;
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_Core.m_ActiveWeapon = m_LastWeapon;
	}

	//SetWeapon(m_LastWeapon);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_SavedInput, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;

	mem_copy(&m_SavedInput, &m_Input, sizeof(m_SavedInput));
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	Antibot()->OnDirectInput(m_pPlayer->GetCID());

	// why is 2 magical?
	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevPrevInput, &m_LatestPrevInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	//m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire & 1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{

	/*if(m_pPlayer->m_ForceBalanced)
	{
		char Buf[128];
		str_format(Buf, sizeof(Buf), "You were moved to %s due to team balancing", GameServer()->m_pController->GetTeamName(m_pPlayer->GetTeam()));
		GameServer()->SendBroadcast(Buf, m_pPlayer->GetCID());

		m_pPlayer->m_ForceBalanced = false;
	}*/

	if(m_Paused)
		return;

	// set emote
	if(m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = m_pPlayer->m_DefEmote;
		m_EmoteStop = -1;
	}

	DDRaceTick();

	Antibot()->OnCharacterTick(m_pPlayer->GetCID());

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);

	if(!m_PrevInput.m_Hook && m_Input.m_Hook && !(m_Core.m_TriggeredEvents & COREEVENT_HOOK_ATTACH_PLAYER))
	{
		Antibot()->OnHookAttach(m_pPlayer->GetCID(), false);
	}

	// handle Weapons
	HandleWeapons();

	// INFCROYA BEGIN ------------------------------------------------------------
	if (m_Poison > 0)
	{
		if (m_PoisonTick == 0)
		{
			m_Poison--;
			TakeDamage(vec2(0.0f, 0.0f), vec2(0, 0), 1, m_PoisonFrom, WEAPON_WORLD);
			GameServer()->SendEmoticon(GetPlayer()->GetCID(), EMOTICON_DROP);
			GameServer()->SendBroadcast("You've stepped into a poison! -4 hp", GetPlayer()->GetCID());
			if (m_Poison > 0)
			{
				m_PoisonTick = Server()->TickSpeed() / 2;
			} else {
				GameServer()->SendEmoticon(GetPlayer()->GetCID(), EMOTICON_SUSHI);
			}
		}
		else
		{
			m_PoisonTick--;
		}
	}
	// INFCROYA END ------------------------------------------------------------//

	if(m_SlowMotionTick > 0)
	{
		m_Core.m_IsInSlowMotion = true;
		float Factor = 1.0f - ((float)g_Config.m_InfSlowMotionPercent / 100);
		float FactorSpeed = 1.0f - ((float)g_Config.m_InfSlowMotionHookSpeed / 100);
		float FactorAccel = 1.0f - ((float)g_Config.m_InfSlowMotionHookAccel / 100);
		// pTuningParams->m_GroundControlSpeed = pTuningParams->m_GroundControlSpeed * Factor;
		// pTuningParams->m_HookFireSpeed = pTuningParams->m_HookFireSpeed * FactorSpeed;
		// pTuningParams->m_AirControlSpeed = pTuningParams->m_AirControlSpeed * Factor;
		// pTuningParams->m_HookDragAccel = pTuningParams->m_HookDragAccel * FactorAccel;
		// pTuningParams->m_HookDragSpeed = pTuningParams->m_HookDragSpeed * FactorSpeed;
		// pTuningParams->m_Gravity = g_Config.m_InfSlowMotionGravity * 0.01f;

		if (g_Config.m_InfSlowMotionMaxSpeed > 0) 
		{
			float MaxSpeed = g_Config.m_InfSlowMotionMaxSpeed * 0.1f;
			float diff = MaxSpeed / length(m_Core.m_Vel);
			if (diff < 1.0f) m_Core.m_Vel *= diff;
			// float MaxSpeed = g_Config.m_InfSlowMotionMaxSpeed * 0.1f;
			// float diffX = MaxSpeed / m_Core.m_Vel.x;
			// float diffY = MaxSpeed / m_Core.m_Vel.y;
			// if (diffX < 1.0f) m_Core.m_Vel.x *= diffX;
			// if (diffY < 1.0f) m_Core.m_Vel.x *= diffY;
		}
	}

	if(m_SlowMotionTick > 0)
	{
		--m_SlowMotionTick;
		
		if(m_SlowMotionTick <= 0)
		{
			m_IsInSlowMotion = false;
			m_Core.m_IsInSlowMotion = false;
		}
		else
		{
			int SloMoSec = 1+(m_SlowMotionTick/Server()->TickSpeed());
		}
	}

	DDRacePostCoreTick();

	// passenger jump to exit
	if(m_Core.m_TriggeredEvents&COREEVENT_AIR_JUMP ||
	   m_Core.m_TriggeredEvents&COREEVENT_GROUND_JUMP)
	{
		if (m_pPlayer && m_pPlayer->GetCharacter() &&
			m_pPlayer->GetCharacter()->m_TaxiPassenger)
		{
			// dbg_msg("taxi", "passenger jumped");
			m_pPlayer->GetCharacter()->m_TaxiPassenger = false;
			if (m_Core.m_TaxiDriverCore)
			{
				CCharacterCore *DriverCore = m_Core.m_TaxiDriverCore;
				m_Core.m_Pos.x = DriverCore->m_Pos.x;
				m_Core.m_Pos.y = DriverCore->m_Pos.y;
				m_Core.m_Vel.x = DriverCore->m_Vel.x;
				m_Core.m_Vel.y = DriverCore->m_Vel.y;
				DriverCore->m_TaxiPassengerCore = nullptr;
				m_Core.m_TaxiDriverCore = nullptr;
			}
		}
	}


	if(m_Core.m_TriggeredEvents&COREEVENT_HOOK_ATTACH_PLAYER)
	{
		if(m_Core.m_HookedPlayer != -1
			&& GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetTeam() != -1
			&& GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetCharacter())
		{
			CCharacter *TargetChar = GameServer()->m_apPlayers[m_Core.m_HookedPlayer]->GetCharacter();
			// enter a taxi
			if (TargetChar->m_FreeTaxi &&
			    !TargetChar->m_TaxiPassenger &&
				!TargetChar->GetCore().m_TaxiPassengerCore &&
				(
					(TargetChar->IsHuman() && IsHuman()) ||
					(TargetChar->IsZombie() && IsZombie())
				)
			)
			{
				// disable hook protection for a driver
				if (TargetChar->GetCroyaPlayer())
					TargetChar->GetCroyaPlayer()->SetHookProtected(false);
				// disable hook protection for self
				if (GetCroyaPlayer())
					GetCroyaPlayer()->SetHookProtected(false);
				TargetChar->ResetTaxi();
				ResetTaxi();
				// hooker is now a passenger and is no longer vacant
				m_pPlayer->GetCharacter()->m_TaxiPassenger = true;
				// driver now has a link to a passenger's core to follow him
				TargetChar->GetpCore()->m_TaxiPassengerCore = &m_Core;
				// let's remember a core of a driver to exit it later
				m_Core.m_TaxiDriverCore = TargetChar->GetpCore();
			}
			// m_Core.m_TaxiPassengerCore = GameWorld()->m_Core.m_apCharacters[m_Core.m_HookedPlayer];
			Antibot()->OnHookAttach(m_pPlayer->GetCID(), true);
		}
	}

	// passenger follows a taxi
	if (m_Core.m_TaxiPassengerCore) {
		m_Core.m_TaxiPassengerCore->m_Pos.x = m_Core.m_Pos.x;
		m_Core.m_TaxiPassengerCore->m_Pos.y = m_Core.m_Pos.y - 60;
		m_Core.m_TaxiPassengerCore->m_Vel.x = m_Core.m_Vel.x;
		m_Core.m_TaxiPassengerCore->m_Vel.y = m_Core.m_Vel.y - 1.5f;
	}


	// Previnput
	m_PrevInput = m_Input;

	m_PrevPos = m_Core.m_Pos;
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision(), &Teams()->m_Core, &((CGameControllerDDRace *)GameServer()->m_pController)->m_TeleOuts);
		m_ReckoningCore.m_Id = m_pPlayer->GetCID();
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.m_Id = m_pPlayer->GetCID();
	m_Core.Move();
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		} StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	{
		int Events = m_Core.m_TriggeredEvents;
		int CID = m_pPlayer->GetCID();

		int64 TeamMask = Teams()->TeamMask(Team(), -1, CID);
		// Some sounds are triggered client-side for the acting player
		// so we need to avoid duplicating them
		int64 TeamMaskExceptSelf = Teams()->TeamMask(Team(), CID, CID);
		// Some are triggered client-side but only on Sixup
		int64 TeamMaskExceptSelfIfSixup = Server()->IsSixup(CID) ? TeamMaskExceptSelf : TeamMask;

		if(Events & COREEVENT_GROUND_JUMP)
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, TeamMaskExceptSelf);

		if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
			GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, TeamMaskExceptSelfIfSixup);

		if(Events & COREEVENT_HOOK_ATTACH_GROUND)
			GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, TeamMaskExceptSelf);

		if(Events & COREEVENT_HOOK_HIT_NOHOOK)
			GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, TeamMaskExceptSelf);
	}

	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_Core.m_pReset || m_ReckoningTick + Server()->TickSpeed() * 3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
			m_Core.m_pReset = false;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_DamageTakenTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_Core.m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_Core.m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
	// INFCROYA BEGIN ------------------------------------------------------------
	++m_HookDmgTick;
	// INFCROYA END ------------------------------------------------------------//
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health + Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor + Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	ResetTaxi();
	if(Server()->IsRecording(m_pPlayer->GetCID()))
		Server()->StopRecord(m_pPlayer->GetCID());

	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
		Killer, Server()->ClientName(Killer),
		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	if (Killer >= 0 && GameServer()->m_apPlayers[Killer])
		GameServer()->m_apPlayers[Killer]->GetCroyaPlayer()->OnKill(GetPlayer()->GetCID());

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));

	// this is to rate limit respawning to 3 secs
	m_pPlayer->m_PreviousDieTick = m_pPlayer->m_DieTick;
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	m_Solo = false;

	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID(), Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
	Teams()->OnCharacterDeath(GetPlayer()->GetCID(), Weapon);
	// INFCROYA BEGIN ------------------------------------------------------------
	DestroyChildEntities();
	Destroy();
	// INFCROYA END ------------------------------------------------------------//
}

bool CCharacter::TakeDamage(vec2 Force, vec2 Source, int Dmg, int From, int Weapon)
{
	if (GetCroyaPlayer() &&
	    GetCroyaPlayer()->GetClassNum() == Class::HUNTER &&
		Weapon == WEAPON_SHOTGUN)
		return TakeDamageDDNet(Force / 4.0, Dmg, From, Weapon);
	else
		return TakeDamageDDNet(Force, Dmg, From, Weapon);
	/* m_Core.m_Vel += Force;

    // 	if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && !g_Config.m_SvTeamdamage)
	//	return false;

	// m_pPlayer only inflicts half damage on self
	if(From == m_pPlayer->GetCID())
		Dmg = maximum(1, Dmg/2);

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}

	if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int64 Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0)
	{
		Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	if (Dmg)
	{
		m_EmoteType = EMOTE_PAIN;
		m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;
	}

	vec2 Temp = m_Core.m_Vel + Force;
	m_Core.m_Vel = ClampVel(m_MoveRestrictions, Temp);

	return true; */
}

bool CCharacter::TakeDamageDDNet(vec2 Force, int Dmg, int From, int Weapon)
{
	if(From != m_pPlayer->GetCID())
		if (GameServer()->m_pController->IsWarmup())
			return false;

	if (m_IsFrozen)
	  return false;

	m_Core.m_Vel += Force;

	if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From))
		return false;

	if(From == m_pPlayer->GetCID())
		//return false; // no self damage
		Dmg = maximum(1, Dmg/2);

	// INFCROYA BEGIN ------------------------------------------------------------
	// search tags: no self harm no selfharm selfhurt self hurt
	if (From == m_pPlayer->GetCID() && Weapon != WEAPON_WORLD) {
		int ClassNum = GetCroyaPlayer()->GetClassNum();
		if ((ClassNum == Class::SOLDIER && m_Core.m_ActiveWeapon == WEAPON_GRENADE) || (ClassNum == Class::SCIENTIST && m_Core.m_ActiveWeapon == WEAPON_LASER)) {
			return false;
		}
		if (ClassNum == Class::HERO && m_Core.m_ActiveWeapon == WEAPON_GRENADE) {
			return false;
		}
		if (ClassNum == Class::BOOMER) {
			return false;
		}
	}
	// INFCROYA END ------------------------------------------------------------//

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}

	int ClassNum = GetCroyaPlayer()->GetClassNum();
	if (ClassNum == Class::PSYCHO) {
		Dmg = minimum(Dmg, g_Config.m_InfPsychoMaxDamage);
	}

	if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int64 Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0)
	{
		m_Health = 1;
		if (g_Config.m_SvFng)
			Freeze(5);
		else {
			m_pPlayer->m_Deaths += 1;
			Die(From, Weapon);
		}

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	if(Dmg)
	{
		m_EmoteType = EMOTE_PAIN;
		m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;
	}

	vec2 Temp = m_Core.m_Vel + Force;
	m_Core.m_Vel = ClampVel(m_MoveRestrictions, Temp);

	return true;
}

//TODO: Move the emote stuff to a function
void CCharacter::SnapCharacter(int SnappingClient, int ID)
{
	CCharacterCore *pCore;
	int Tick, Emote = m_EmoteType, Weapon = m_Core.m_ActiveWeapon, AmmoCount = 0,
		  Health = 0, Armor = 0;
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		Tick = 0;
		pCore = &m_Core;
	}
	else
	{
		Tick = m_ReckoningTick;
		pCore = &m_SendCore;
	}

	// change eyes and use ninja graphic if player is frozen
	if(m_DeepFreeze || m_FreezeTime > 0 || m_FreezeTime == -1)
	{
		if(Emote == EMOTE_NORMAL)
			Emote = m_DeepFreeze ? EMOTE_PAIN : EMOTE_BLINK;

		Weapon = WEAPON_NINJA;
	}

	// This could probably happen when m_Jetpack changes instead
	// jetpack and ninjajetpack prediction
	if(m_pPlayer->GetCID() == SnappingClient)
	{
		if(m_Jetpack && Weapon != WEAPON_NINJA)
		{
			if(!(m_NeededFaketuning & FAKETUNE_JETPACK))
			{
				m_NeededFaketuning |= FAKETUNE_JETPACK;
				GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone);
			}
		}
		else
		{
			if(m_NeededFaketuning & FAKETUNE_JETPACK)
			{
				m_NeededFaketuning &= ~FAKETUNE_JETPACK;
				GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone);
			}
		}
	}

	// change eyes, use ninja graphic and set ammo count if player has ninjajetpack
	if(m_pPlayer->m_NinjaJetpack && m_Jetpack && m_Core.m_ActiveWeapon == WEAPON_GUN && !m_DeepFreeze && !(m_FreezeTime > 0 || m_FreezeTime == -1))
	{
		if(Emote == EMOTE_NORMAL)
			Emote = EMOTE_HAPPY;
		Weapon = WEAPON_NINJA;
		AmmoCount = 10;
	}

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		Health = m_Health;
		Armor = m_Armor;
		if(m_aWeapons[m_Core.m_ActiveWeapon].m_Ammo > 0)
			AmmoCount = (!m_FreezeTime) ? m_aWeapons[m_Core.m_ActiveWeapon].m_Ammo : 0;
	}

	if(GetPlayer()->m_Afk || GetPlayer()->IsPaused())
	{
		if(m_FreezeTime > 0 || m_FreezeTime == -1 || m_DeepFreeze)
			Emote = EMOTE_NORMAL;
		else
			Emote = EMOTE_BLINK;
	}

	if(Emote == EMOTE_NORMAL)
	{
		if(250 - ((Server()->Tick() - m_LastAction) % (250)) < 5)
			Emote = EMOTE_BLINK;
	}

	if(m_pPlayer->m_Halloween)
	{
		if(1200 - ((Server()->Tick() - m_LastAction) % (1200)) < 5)
		{
			GameServer()->SendEmoticon(m_pPlayer->GetCID(), EMOTICON_GHOST);
		}
	}

	if (m_IsBot && m_Core.m_BotOwnerCore)
		m_Input.m_Direction = m_Core.m_BotOwnerCore->m_BotInput.m_Direction;

	if(!Server()->IsSixup(SnappingClient))
	{
		CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, ID, sizeof(CNetObj_Character)));
		if(!pCharacter)
			return;

		pCore->Write(pCharacter);

		pCharacter->m_Tick = Tick;
		pCharacter->m_Emote = Emote;

		if(pCharacter->m_HookedPlayer != -1)
		{
			if(!Server()->Translate(pCharacter->m_HookedPlayer, SnappingClient))
				pCharacter->m_HookedPlayer = -1;
		}

		pCharacter->m_AttackTick = m_AttackTick;
		pCharacter->m_Direction = m_Input.m_Direction;
		pCharacter->m_Weapon = Weapon;
		pCharacter->m_AmmoCount = AmmoCount;
		pCharacter->m_Health = Health;
		pCharacter->m_Armor = Armor;
		pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
	}
	else
	{
		protocol7::CNetObj_Character *pCharacter = static_cast<protocol7::CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, ID, sizeof(protocol7::CNetObj_Character)));
		if(!pCharacter)
			return;

		pCore->Write(reinterpret_cast<CNetObj_CharacterCore *>(static_cast<protocol7::CNetObj_CharacterCore *>(pCharacter)));

		pCharacter->m_Tick = Tick;
		pCharacter->m_Emote = Emote;
		pCharacter->m_AttackTick = m_AttackTick;
		pCharacter->m_Direction = m_Input.m_Direction;
		pCharacter->m_Weapon = Weapon;
		pCharacter->m_AmmoCount = AmmoCount;

		if(m_FreezeTime > 0 || m_FreezeTime == -1 || m_DeepFreeze)
			pCharacter->m_AmmoCount = m_FreezeTick + g_Config.m_SvFreezeDelay * Server()->TickSpeed();
		else if(Weapon == WEAPON_NINJA)
			pCharacter->m_AmmoCount = m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000;

		pCharacter->m_Health = Health;
		pCharacter->m_Armor = Armor;
		pCharacter->m_TriggeredEvents = 0;
	}

		// INFCROYA BEGIN ------------------------------------------------------------
	// protected icon
	CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
	if (m_HookProtected) {
		float AngleStart = 2.0f * pi * Server()->Tick() / static_cast<float>(Server()->TickSpeed() * 4);
		float AngleStep = 2.0f * pi / 3;
		AngleStart = AngleStart * 2.0f;
		vec2 PosStart = m_Pos + vec2(40 * cos(AngleStart + AngleStep), 40 * sin(AngleStart + AngleStep));

		CNetObj_Projectile * pObj = static_cast<CNetObj_Projectile*>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_HookProtID, sizeof(CNetObj_Projectile)));
		if (pObj)
		{
			pObj->m_X = (int)PosStart.x;
			pObj->m_Y = (int)PosStart.y;
			pObj->m_VelX = 0;
			pObj->m_VelY = 0;
			pObj->m_StartTick = Server()->Tick() - 200;
			pObj->m_Type = WEAPON_HAMMER;
		}
	}
	// Taxi icon
	if (m_FreeTaxi) {
		if (Server()->IsSixup(SnappingClient)) {
			protocol7::CNetObj_Pickup* pP = static_cast<protocol7::CNetObj_Pickup*>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_TaxiID, sizeof(protocol7::CNetObj_Pickup)));
			if (!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			pP->m_Type = 5;
		} else {
			CNetObj_Pickup* pP = static_cast<CNetObj_Pickup*>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_TaxiID, sizeof(CNetObj_Pickup)));
			if (!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			pP->m_Type = POWERUP_NINJA;
			pP->m_Subtype = WEAPON_NINJA;
		}
	}
	// Heart displayed on top of injured tees
	if (IsZombie() && GetHealthArmorSum() < 10 && SnappingClient != m_pPlayer->GetCID() && pClient->GetCroyaPlayer()->IsZombie()) {
		if (Server()->IsSixup(SnappingClient)) {
			protocol7::CNetObj_Pickup* pP = static_cast<protocol7::CNetObj_Pickup*>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(protocol7::CNetObj_Pickup)));
			if (!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			pP->m_Type = POWERUP_HEALTH;
		} else {
			CNetObj_Pickup* pP = static_cast<CNetObj_Pickup*>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
			if (!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			pP->m_Type = POWERUP_HEALTH;
		}
	}
	if (IsHuman() && m_Armor < 10 && SnappingClient != m_pPlayer->GetCID() && pClient->GetCroyaPlayer()->GetClassNum() == Class::MEDIC) {
		if (Server()->IsSixup(SnappingClient)) {
			protocol7::CNetObj_Pickup* pP = static_cast<protocol7::CNetObj_Pickup*>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(protocol7::CNetObj_Pickup)));
			if (!pP)
				return;
			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			if (m_Health < 10)
				pP->m_Type = POWERUP_HEALTH;
			else if (m_Armor < 10)
				pP->m_Type = POWERUP_ARMOR;
		} else {
			CNetObj_Pickup* pP = static_cast<CNetObj_Pickup*>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
			if (!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			if (m_Health < 10)
				pP->m_Type = POWERUP_HEALTH;
			else if (m_Armor < 10)
				pP->m_Type = POWERUP_ARMOR;
		}
	}
	if (!m_FirstShot)
	{
		CNetObj_Laser* pObj = static_cast<CNetObj_Laser*>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_BarrierHintID, sizeof(CNetObj_Laser)));
		if (!pObj)
			return;

		pObj->m_X = (int)m_FirstShotCoord.x;
		pObj->m_Y = (int)m_FirstShotCoord.y;
		pObj->m_FromX = (int)m_FirstShotCoord.x;
		pObj->m_FromY = (int)m_FirstShotCoord.y;
		pObj->m_StartTick = Server()->Tick();
	}
	// INFCROYA END ------------------------------------------------------------//
}

void CCharacter::Snap(int SnappingClient)
{
	int ID = m_pPlayer->GetCID();

	if(SnappingClient > -1 && !Server()->Translate(ID, SnappingClient))
		return;

	if(NetworkClipped(SnappingClient))
		return;

	if(SnappingClient > -1)
	{
		CCharacter *pSnapChar = GameServer()->GetPlayerChar(SnappingClient);
		CPlayer *pSnapPlayer = GameServer()->m_apPlayers[SnappingClient];

		if(pSnapPlayer->GetTeam() == TEAM_SPECTATORS || pSnapPlayer->IsPaused())
		{
			if(pSnapPlayer->m_SpectatorID != -1 && !CanCollide(pSnapPlayer->m_SpectatorID) && (pSnapPlayer->m_ShowOthers == 0 || (pSnapPlayer->m_ShowOthers == 2 && !SameTeam(pSnapPlayer->m_SpectatorID))))
				return;
			else if(pSnapPlayer->m_SpectatorID == -1 && !CanCollide(SnappingClient) && pSnapPlayer->m_SpecTeam)
				return;
		}
		else if(pSnapChar && !pSnapChar->m_Super && !CanCollide(SnappingClient) && (pSnapPlayer->m_ShowOthers == 0 || (pSnapPlayer->m_ShowOthers == 2 && !SameTeam(SnappingClient))))
			return;
	}

	if(m_Paused)
		return;

	SnapCharacter(SnappingClient, ID);

	CNetObj_DDNetCharacter *pDDNetCharacter = static_cast<CNetObj_DDNetCharacter *>(Server()->SnapNewItem(NETOBJTYPE_DDNETCHARACTER, ID, sizeof(CNetObj_DDNetCharacter)));
	if(!pDDNetCharacter)
		return;

	pDDNetCharacter->m_Flags = 0;
	if(m_Solo)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_SOLO;
	if(m_Super)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_SUPER;
	if(m_EndlessHook)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_ENDLESS_HOOK;
	if(!m_Core.m_Collision)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_COLLISION;
	if(!m_Core.m_Hook)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_HOOK;
	if(m_SuperJump)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_ENDLESS_JUMP;
	if(m_Jetpack || m_NinjaJetpack)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_JETPACK;
	if(m_Hit & DISABLE_HIT_GRENADE)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_GRENADE_HIT;
	if(m_Hit & DISABLE_HIT_HAMMER)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_HAMMER_HIT;
	if(m_Hit & DISABLE_HIT_LASER)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_LASER_HIT;
	if(m_Hit & DISABLE_HIT_SHOTGUN)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_NO_SHOTGUN_HIT;
	if(m_Core.m_HasTelegunGun)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_TELEGUN_GUN;
	if(m_Core.m_HasTelegunGrenade)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_TELEGUN_GRENADE;
	if(m_Core.m_HasTelegunLaser)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_TELEGUN_LASER;
	if(m_aWeapons[WEAPON_HAMMER].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_HAMMER;
	if(m_aWeapons[WEAPON_GUN].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_GUN;
	if(m_aWeapons[WEAPON_SHOTGUN].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_SHOTGUN;
	if(m_aWeapons[WEAPON_GRENADE].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_GRENADE;
	if(m_aWeapons[WEAPON_LASER].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_LASER;
	if(m_Core.m_ActiveWeapon == WEAPON_NINJA)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_NINJA;

	pDDNetCharacter->m_FreezeEnd = m_DeepFreeze ? -1 : m_FreezeTime == 0 ? 0 : Server()->Tick() + m_FreezeTime;
	pDDNetCharacter->m_Jumps = m_Core.m_Jumps;
	pDDNetCharacter->m_TeleCheckpoint = m_TeleCheckpoint;
	pDDNetCharacter->m_StrongWeakID = m_StrongWeakID;
}

int CCharacter::NetworkClipped(int SnappingClient)
{
	return NetworkClipped(SnappingClient, m_Pos);
}

int CCharacter::NetworkClipped(int SnappingClient, vec2 CheckPos)
{
	if(SnappingClient == -1 || GameServer()->m_apPlayers[SnappingClient]->m_ShowAll)
		return 0;

	float dx = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.x - CheckPos.x;
	if(absolute(dx) > GameServer()->m_apPlayers[SnappingClient]->m_ShowDistance.x)
		return 1;

	float dy = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.y - CheckPos.y;
	if(absolute(dy) > GameServer()->m_apPlayers[SnappingClient]->m_ShowDistance.y)
		return 1;

	return 0;
}

// DDRace

bool CCharacter::CanCollideInf(int ClientID)
{
	if (ClientID < 0)
	    return false;
	if (!GameServer()->m_apPlayers[ClientID])
		return false;
	if (!GameServer()->m_apPlayers[ClientID]->GetCharacter())
		return false;
	bool IsTargetZombie = GameServer()->m_apPlayers[ClientID]->GetCharacter()->IsZombie();
	bool IsTargetHuman = !IsTargetZombie;
	if (IsZombie() && IsTargetZombie)
	  return false;
	if (IsHuman() && IsTargetHuman)
	  return false;
	if (IsZombie() && IsTargetHuman)
	  return true;
	if (IsHuman() && IsTargetZombie)
	  return true;
	return true;
}

bool CCharacter::CanCollide(int ClientID)
{
	return Teams()->m_Core.CanCollide(GetPlayer()->GetCID(), ClientID);
}

bool CCharacter::SameTeam(int ClientID)
{
	return Teams()->m_Core.SameTeam(GetPlayer()->GetCID(), ClientID);
}

int CCharacter::Team()
{
	return Teams()->m_Core.Team(m_pPlayer->GetCID());
}

CGameTeams *CCharacter::Teams()
{
	return &((CGameControllerDDRace *)GameServer()->m_pController)->m_Teams;
}

void CCharacter::FillAntibot(CAntibotCharacterData *pData)
{
	pData->m_Pos = m_Pos;
	pData->m_Vel = m_Core.m_Vel;
	pData->m_Angle = m_Core.m_Angle;
	pData->m_HookedPlayer = m_Core.m_HookedPlayer;
	pData->m_SpawnTick = m_SpawnTick;
	pData->m_WeaponChangeTick = m_WeaponChangeTick;
	pData->m_aLatestInputs[0].m_TargetX = m_LatestInput.m_TargetX;
	pData->m_aLatestInputs[0].m_TargetY = m_LatestInput.m_TargetY;
	pData->m_aLatestInputs[1].m_TargetX = m_LatestPrevInput.m_TargetX;
	pData->m_aLatestInputs[1].m_TargetY = m_LatestPrevInput.m_TargetY;
	pData->m_aLatestInputs[2].m_TargetX = m_LatestPrevPrevInput.m_TargetX;
	pData->m_aLatestInputs[2].m_TargetY = m_LatestPrevPrevInput.m_TargetY;
}

void CCharacter::HandleBroadcast()
{
	CPlayerData *pData = GameServer()->Score()->PlayerData(m_pPlayer->GetCID());

	if(m_DDRaceState == DDRACE_STARTED && m_CpLastBroadcast != m_CpActive &&
		m_CpActive > -1 && m_CpTick > Server()->Tick() && m_pPlayer->GetClientVersion() == VERSION_VANILLA &&
		pData->m_BestTime && pData->m_aBestCpTime[m_CpActive] != 0)
	{
		char aBroadcast[128];
		float Diff = m_CpCurrent[m_CpActive] - pData->m_aBestCpTime[m_CpActive];
		str_format(aBroadcast, sizeof(aBroadcast), "Checkpoint | Diff : %+5.2f", Diff);
		GameServer()->SendBroadcast(aBroadcast, m_pPlayer->GetCID());
		m_CpLastBroadcast = m_CpActive;
		m_LastBroadcast = Server()->Tick();
	}
	else if((m_pPlayer->m_TimerType == CPlayer::TIMERTYPE_BROADCAST || m_pPlayer->m_TimerType == CPlayer::TIMERTYPE_GAMETIMER_AND_BROADCAST) && m_DDRaceState == DDRACE_STARTED && m_LastBroadcast + Server()->TickSpeed() * g_Config.m_SvTimeInBroadcastInterval <= Server()->Tick())
	{
		char aBuftime[64];
		int IntTime = (int)((float)(Server()->Tick() - m_StartTime) / ((float)Server()->TickSpeed()));
		str_format(aBuftime, sizeof(aBuftime), "%s%d:%s%d", ((IntTime / 60) > 9) ? "" : "0", IntTime / 60, ((IntTime % 60) > 9) ? "" : "0", IntTime % 60);
		GameServer()->SendBroadcast(aBuftime, m_pPlayer->GetCID(), false);
		m_CpLastBroadcast = m_CpActive;
		m_LastBroadcast = Server()->Tick();
	}
}

void CCharacter::HandleSkippableTiles(int Index)
{
	// handle death-tiles and leaving gamelayer
	if((GameServer()->Collision()->GetCollisionAt(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f) == TILE_DEATH ||
		   GameServer()->Collision()->GetCollisionAt(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f) == TILE_DEATH ||
		   GameServer()->Collision()->GetCollisionAt(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f) == TILE_DEATH ||
		   GameServer()->Collision()->GetFCollisionAt(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f) == TILE_DEATH ||
		   GameServer()->Collision()->GetFCollisionAt(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f) == TILE_DEATH ||
		   GameServer()->Collision()->GetFCollisionAt(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f) == TILE_DEATH ||
		   GameServer()->Collision()->GetCollisionAt(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f) == TILE_DEATH) &&
		!m_Super && !(Team() && Teams()->TeeFinished(m_pPlayer->GetCID())))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		return;
	}

	if(GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		return;
	}

	if(Index < 0)
		return;

	// handle speedup tiles
	if(GameServer()->Collision()->IsSpeedup(Index))
	{
		vec2 Direction, MaxVel, TempVel = m_Core.m_Vel;
		int Force, MaxSpeed = 0;
		float TeeAngle, SpeederAngle, DiffAngle, SpeedLeft, TeeSpeed;
		GameServer()->Collision()->GetSpeedup(Index, &Direction, &Force, &MaxSpeed);
		if(Force == 255 && MaxSpeed)
		{
			m_Core.m_Vel = Direction * (MaxSpeed / 5);
		}
		else
		{
			if(MaxSpeed > 0 && MaxSpeed < 5)
				MaxSpeed = 5;
			if(MaxSpeed > 0)
			{
				if(Direction.x > 0.0000001f)
					SpeederAngle = -atan(Direction.y / Direction.x);
				else if(Direction.x < 0.0000001f)
					SpeederAngle = atan(Direction.y / Direction.x) + 2.0f * asin(1.0f);
				else if(Direction.y > 0.0000001f)
					SpeederAngle = asin(1.0f);
				else
					SpeederAngle = asin(-1.0f);

				if(SpeederAngle < 0)
					SpeederAngle = 4.0f * asin(1.0f) + SpeederAngle;

				if(TempVel.x > 0.0000001f)
					TeeAngle = -atan(TempVel.y / TempVel.x);
				else if(TempVel.x < 0.0000001f)
					TeeAngle = atan(TempVel.y / TempVel.x) + 2.0f * asin(1.0f);
				else if(TempVel.y > 0.0000001f)
					TeeAngle = asin(1.0f);
				else
					TeeAngle = asin(-1.0f);

				if(TeeAngle < 0)
					TeeAngle = 4.0f * asin(1.0f) + TeeAngle;

				TeeSpeed = sqrt(pow(TempVel.x, 2) + pow(TempVel.y, 2));

				DiffAngle = SpeederAngle - TeeAngle;
				SpeedLeft = MaxSpeed / 5.0f - cos(DiffAngle) * TeeSpeed;
				if(abs((int)SpeedLeft) > Force && SpeedLeft > 0.0000001f)
					TempVel += Direction * Force;
				else if(abs((int)SpeedLeft) > Force)
					TempVel += Direction * -Force;
				else
					TempVel += Direction * SpeedLeft;
			}
			else
				TempVel += Direction * Force;

			m_Core.m_Vel = ClampVel(m_MoveRestrictions, TempVel);
		}
	}
}

bool CCharacter::IsSwitchActiveCb(int Number, void *pUser)
{
	CCharacter *pThis = (CCharacter *)pUser;
	CCollision *pCollision = pThis->GameServer()->Collision();
	return pCollision->m_pSwitchers && pCollision->m_pSwitchers[Number].m_Status[pThis->Team()] && pThis->Team() != TEAM_SUPER;
}

void CCharacter::HandleTiles(int Index)
{
	CGameControllerDDRace *Controller = (CGameControllerDDRace *)GameServer()->m_pController;
	int MapIndex = Index;
	//int PureMapIndex = GameServer()->Collision()->GetPureMapIndex(m_Pos);
	m_TileIndex = GameServer()->Collision()->GetTileIndex(MapIndex);
	m_TileFIndex = GameServer()->Collision()->GetFTileIndex(MapIndex);
	m_MoveRestrictions = GameServer()->Collision()->GetMoveRestrictions(IsSwitchActiveCb, this, m_Pos, 18.0f, MapIndex);
	//Sensitivity
	int S1 = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f));
	int S2 = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f));
	int S3 = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f));
	int S4 = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f));
	int Tile1 = GameServer()->Collision()->GetTileIndex(S1);
	int Tile2 = GameServer()->Collision()->GetTileIndex(S2);
	int Tile3 = GameServer()->Collision()->GetTileIndex(S3);
	int Tile4 = GameServer()->Collision()->GetTileIndex(S4);
	int FTile1 = GameServer()->Collision()->GetFTileIndex(S1);
	int FTile2 = GameServer()->Collision()->GetFTileIndex(S2);
	int FTile3 = GameServer()->Collision()->GetFTileIndex(S3);
	int FTile4 = GameServer()->Collision()->GetFTileIndex(S4);
	if(Index < 0)
	{
		m_LastRefillJumps = false;
		m_LastPenalty = false;
		m_LastBonus = false;
		return;
	}
	int cp = GameServer()->Collision()->IsCheckpoint(MapIndex);
	if(cp != -1 && m_DDRaceState == DDRACE_STARTED && cp > m_CpActive)
	{
		m_CpActive = cp;
		m_CpCurrent[cp] = m_Time;
		m_CpTick = Server()->Tick() + Server()->TickSpeed() * 2;
		if(m_pPlayer->GetClientVersion() >= VERSION_DDRACE)
		{
			CPlayerData *pData = GameServer()->Score()->PlayerData(m_pPlayer->GetCID());
			CNetMsg_Sv_DDRaceTime Msg;
			Msg.m_Time = (int)m_Time;
			Msg.m_Check = 0;
			Msg.m_Finish = 0;

			if(m_CpActive != -1 && m_CpTick > Server()->Tick())
			{
				if(pData->m_BestTime && pData->m_aBestCpTime[m_CpActive] != 0)
				{
					float Diff = (m_CpCurrent[m_CpActive] - pData->m_aBestCpTime[m_CpActive]) * 100;
					Msg.m_Check = (int)Diff;
				}
			}

			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
		}
	}
	int cpf = GameServer()->Collision()->IsFCheckpoint(MapIndex);
	if(cpf != -1 && m_DDRaceState == DDRACE_STARTED && cpf > m_CpActive)
	{
		m_CpActive = cpf;
		m_CpCurrent[cpf] = m_Time;
		m_CpTick = Server()->Tick() + Server()->TickSpeed() * 2;
		if(m_pPlayer->GetClientVersion() >= VERSION_DDRACE)
		{
			CPlayerData *pData = GameServer()->Score()->PlayerData(m_pPlayer->GetCID());
			CNetMsg_Sv_DDRaceTime Msg;
			Msg.m_Time = (int)m_Time;
			Msg.m_Check = 0;
			Msg.m_Finish = 0;

			if(m_CpActive != -1 && m_CpTick > Server()->Tick())
			{
				if(pData->m_BestTime && pData->m_aBestCpTime[m_CpActive] != 0)
				{
					float Diff = (m_CpCurrent[m_CpActive] - pData->m_aBestCpTime[m_CpActive]) * 100;
					Msg.m_Check = (int)Diff;
				}
			}

			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
		}
	}
	int tcp = GameServer()->Collision()->IsTCheckpoint(MapIndex);
	if(tcp)
		m_TeleCheckpoint = tcp;

	// start
	if(((m_TileIndex == TILE_START) || (m_TileFIndex == TILE_START) || FTile1 == TILE_START || FTile2 == TILE_START || FTile3 == TILE_START || FTile4 == TILE_START || Tile1 == TILE_START || Tile2 == TILE_START || Tile3 == TILE_START || Tile4 == TILE_START) && (m_DDRaceState == DDRACE_NONE || m_DDRaceState == DDRACE_FINISHED || (m_DDRaceState == DDRACE_STARTED && !Team() && g_Config.m_SvTeam != 3)))
	{
		if(Teams()->GetSaving(Team()))
		{
			if(m_LastStartWarning < Server()->Tick() - 3 * Server()->TickSpeed())
			{
				GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't start while loading/saving of team is in progress");
				m_LastStartWarning = Server()->Tick();
			}
			Die(GetPlayer()->GetCID(), WEAPON_WORLD);
			return;
		}
		if(g_Config.m_SvTeam == 2 && (Team() == TEAM_FLOCK || Teams()->Count(Team()) <= 1))
		{
			if(m_LastStartWarning < Server()->Tick() - 3 * Server()->TickSpeed())
			{
				GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You have to be in a team with other tees to start");
				m_LastStartWarning = Server()->Tick();
			}
			Die(GetPlayer()->GetCID(), WEAPON_WORLD);
			return;
		}
		if(g_Config.m_SvResetPickups)
		{
			for(int i = WEAPON_SHOTGUN; i < NUM_WEAPONS; ++i)
			{
				m_aWeapons[i].m_Got = false;
				if(m_Core.m_ActiveWeapon == i)
					m_Core.m_ActiveWeapon = WEAPON_GUN;
			}
		}

		Teams()->OnCharacterStart(m_pPlayer->GetCID());
		m_CpActive = -2;
	}

	// finish
	if(((m_TileIndex == TILE_FINISH) || (m_TileFIndex == TILE_FINISH) || FTile1 == TILE_FINISH || FTile2 == TILE_FINISH || FTile3 == TILE_FINISH || FTile4 == TILE_FINISH || Tile1 == TILE_FINISH || Tile2 == TILE_FINISH || Tile3 == TILE_FINISH || Tile4 == TILE_FINISH) && m_DDRaceState == DDRACE_STARTED)
		Controller->m_Teams.OnCharacterFinish(m_pPlayer->GetCID());

	// freeze
	if(((m_TileIndex == TILE_FREEZE) || (m_TileFIndex == TILE_FREEZE)) && !m_Super && !m_DeepFreeze)
		Freeze();
	else if(((m_TileIndex == TILE_UNFREEZE) || (m_TileFIndex == TILE_UNFREEZE)) && !m_DeepFreeze)
		UnFreeze();

	// deep freeze
	if(((m_TileIndex == TILE_DFREEZE) || (m_TileFIndex == TILE_DFREEZE)) && !m_Super && !m_DeepFreeze)
		m_DeepFreeze = true;
	else if(((m_TileIndex == TILE_DUNFREEZE) || (m_TileFIndex == TILE_DUNFREEZE)) && !m_Super && m_DeepFreeze)
		m_DeepFreeze = false;

	// endless hook
	if(((m_TileIndex == TILE_EHOOK_ENABLE) || (m_TileFIndex == TILE_EHOOK_ENABLE)) && !m_EndlessHook)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Endless hook has been activated");
		m_EndlessHook = true;
		m_Core.m_EndlessHook = true;
	}
	else if(((m_TileIndex == TILE_EHOOK_DISABLE) || (m_TileFIndex == TILE_EHOOK_DISABLE)) && m_EndlessHook)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Endless hook has been deactivated");
		m_EndlessHook = false;
		m_Core.m_EndlessHook = false;
	}

	// hit others
	if(((m_TileIndex == TILE_HIT_DISABLE) || (m_TileFIndex == TILE_HIT_DISABLE)) && m_Hit != (DISABLE_HIT_GRENADE | DISABLE_HIT_HAMMER | DISABLE_HIT_LASER | DISABLE_HIT_SHOTGUN))
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't hit others");
		m_Hit = DISABLE_HIT_GRENADE | DISABLE_HIT_HAMMER | DISABLE_HIT_LASER | DISABLE_HIT_SHOTGUN;
		m_Core.m_NoShotgunHit = true;
		m_Core.m_NoGrenadeHit = true;
		m_Core.m_NoHammerHit = true;
		m_Core.m_NoLaserHit = true;
		m_NeededFaketuning |= FAKETUNE_NOHAMMER;
		GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
	}
	else if(((m_TileIndex == TILE_HIT_ENABLE) || (m_TileFIndex == TILE_HIT_ENABLE)) && m_Hit != HIT_ALL)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can hit others");
		m_Hit = HIT_ALL;
		m_Core.m_NoShotgunHit = false;
		m_Core.m_NoGrenadeHit = false;
		m_Core.m_NoHammerHit = false;
		m_Core.m_NoLaserHit = false;
		m_NeededFaketuning &= ~FAKETUNE_NOHAMMER;
		GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
	}

	// collide with others
	if(((m_TileIndex == TILE_NPC_DISABLE) || (m_TileFIndex == TILE_NPC_DISABLE)) && m_Core.m_Collision)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't collide with others");
		m_Core.m_Collision = false;
		m_Core.m_NoCollision = true;
		m_NeededFaketuning |= FAKETUNE_NOCOLL;
		GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
	}
	else if(((m_TileIndex == TILE_NPC_ENABLE) || (m_TileFIndex == TILE_NPC_ENABLE)) && !m_Core.m_Collision)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can collide with others");
		m_Core.m_Collision = true;
		m_Core.m_NoCollision = false;
		m_NeededFaketuning &= ~FAKETUNE_NOCOLL;
		GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
	}

	// hook others
	if(((m_TileIndex == TILE_NPH_DISABLE) || (m_TileFIndex == TILE_NPH_DISABLE)) && m_Core.m_Hook)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't hook others");
		m_Core.m_Hook = false;
		m_Core.m_NoHookHit = true;
		m_NeededFaketuning |= FAKETUNE_NOHOOK;
		GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
	}
	else if(((m_TileIndex == TILE_NPH_ENABLE) || (m_TileFIndex == TILE_NPH_ENABLE)) && !m_Core.m_Hook)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can hook others");
		m_Core.m_Hook = true;
		m_Core.m_NoHookHit = false;
		m_NeededFaketuning &= ~FAKETUNE_NOHOOK;
		GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
	}

	// unlimited air jumps
	if(((m_TileIndex == TILE_UNLIMITED_JUMPS_ENABLE) || (m_TileFIndex == TILE_UNLIMITED_JUMPS_ENABLE)) && !m_SuperJump)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You have unlimited air jumps");
		m_SuperJump = true;
		m_Core.m_EndlessJump = true;
		if(m_Core.m_Jumps == 0)
		{
			m_NeededFaketuning &= ~FAKETUNE_NOJUMP;
			GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
		}
	}
	else if(((m_TileIndex == TILE_UNLIMITED_JUMPS_DISABLE) || (m_TileFIndex == TILE_UNLIMITED_JUMPS_DISABLE)) && m_SuperJump)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You don't have unlimited air jumps");
		m_SuperJump = false;
		m_Core.m_EndlessJump = false;
		if(m_Core.m_Jumps == 0)
		{
			m_NeededFaketuning |= FAKETUNE_NOJUMP;
			GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
		}
	}

	// walljump
	if((m_TileIndex == TILE_WALLJUMP) || (m_TileFIndex == TILE_WALLJUMP))
	{
		if(m_Core.m_Vel.y > 0 && m_Core.m_Colliding && m_Core.m_LeftWall)
		{
			m_Core.m_LeftWall = false;
			m_Core.m_JumpedTotal = m_Core.m_Jumps - 1;
			m_Core.m_Jumped = 1;
		}
	}

	// jetpack gun
	if(((m_TileIndex == TILE_JETPACK_ENABLE) || (m_TileFIndex == TILE_JETPACK_ENABLE)) && !m_Jetpack)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You have a jetpack gun");
		m_Jetpack = true;
		m_Core.m_Jetpack = true;
	}
	else if(((m_TileIndex == TILE_JETPACK_DISABLE) || (m_TileFIndex == TILE_JETPACK_DISABLE)) && m_Jetpack)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You lost your jetpack gun");
		m_Jetpack = false;
		m_Core.m_Jetpack = false;
	}

	// unlock team
	else if(((m_TileIndex == TILE_UNLOCK_TEAM) || (m_TileFIndex == TILE_UNLOCK_TEAM)) && Teams()->TeamLocked(Team()))
	{
		Teams()->SetTeamLock(Team(), false);

		for(int i = 0; i < MAX_CLIENTS; i++)
			if(Teams()->m_Core.Team(i) == Team())
				GameServer()->SendChatTarget(i, "Your team was unlocked by an unlock team tile");
	}

	// solo part
	if(((m_TileIndex == TILE_SOLO_ENABLE) || (m_TileFIndex == TILE_SOLO_ENABLE)) && !Teams()->m_Core.GetSolo(m_pPlayer->GetCID()))
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You are now in a solo part");
		SetSolo(true);
	}
	else if(((m_TileIndex == TILE_SOLO_DISABLE) || (m_TileFIndex == TILE_SOLO_DISABLE)) && Teams()->m_Core.GetSolo(m_pPlayer->GetCID()))
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You are now out of the solo part");
		SetSolo(false);
	}

	// refill jumps
	if(((m_TileIndex == TILE_REFILL_JUMPS) || (m_TileFIndex == TILE_REFILL_JUMPS)) && !m_LastRefillJumps)
	{
		m_Core.m_JumpedTotal = 0;
		m_Core.m_Jumped = 0;
		m_LastRefillJumps = true;
	}
	if((m_TileIndex != TILE_REFILL_JUMPS) && (m_TileFIndex != TILE_REFILL_JUMPS))
	{
		m_LastRefillJumps = false;
	}

	// Teleport gun
	if(((m_TileIndex == TILE_TELE_GUN_ENABLE) || (m_TileFIndex == TILE_TELE_GUN_ENABLE)) && !m_Core.m_HasTelegunGun)
	{
		m_Core.m_HasTelegunGun = true;
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Teleport gun enabled");
	}
	else if(((m_TileIndex == TILE_TELE_GUN_DISABLE) || (m_TileFIndex == TILE_TELE_GUN_DISABLE)) && m_Core.m_HasTelegunGun)
	{
		m_Core.m_HasTelegunGun = false;
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Teleport gun disabled");
	}

	if(((m_TileIndex == TILE_TELE_GRENADE_ENABLE) || (m_TileFIndex == TILE_TELE_GRENADE_ENABLE)) && !m_Core.m_HasTelegunGrenade)
	{
		m_Core.m_HasTelegunGrenade = true;
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Teleport grenade enabled");
	}
	else if(((m_TileIndex == TILE_TELE_GRENADE_DISABLE) || (m_TileFIndex == TILE_TELE_GRENADE_DISABLE)) && m_Core.m_HasTelegunGrenade)
	{
		m_Core.m_HasTelegunGrenade = false;
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Teleport grenade disabled");
	}

	if(((m_TileIndex == TILE_TELE_LASER_ENABLE) || (m_TileFIndex == TILE_TELE_LASER_ENABLE)) && !m_Core.m_HasTelegunLaser)
	{
		m_Core.m_HasTelegunLaser = true;
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Teleport laser enabled");
	}
	else if(((m_TileIndex == TILE_TELE_LASER_DISABLE) || (m_TileFIndex == TILE_TELE_LASER_DISABLE)) && m_Core.m_HasTelegunLaser)
	{
		m_Core.m_HasTelegunLaser = false;
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Teleport laser disabled");
	}

	// stopper
	if(m_Core.m_Vel.y > 0 && (m_MoveRestrictions & CANTMOVE_DOWN))
	{
		m_Core.m_Jumped = 0;
		m_Core.m_JumpedTotal = 0;
	}
	m_Core.m_Vel = ClampVel(m_MoveRestrictions, m_Core.m_Vel);

	// handle switch tiles
	if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SWITCHOPEN && Team() != TEAM_SUPER && GameServer()->Collision()->GetSwitchNumber(MapIndex) > 0)
	{
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()] = true;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_EndTick[Team()] = 0;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Type[Team()] = TILE_SWITCHOPEN;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SWITCHTIMEDOPEN && Team() != TEAM_SUPER && GameServer()->Collision()->GetSwitchNumber(MapIndex) > 0)
	{
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()] = true;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_EndTick[Team()] = Server()->Tick() + 1 + GameServer()->Collision()->GetSwitchDelay(MapIndex) * Server()->TickSpeed();
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Type[Team()] = TILE_SWITCHTIMEDOPEN;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SWITCHTIMEDCLOSE && Team() != TEAM_SUPER && GameServer()->Collision()->GetSwitchNumber(MapIndex) > 0)
	{
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()] = false;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_EndTick[Team()] = Server()->Tick() + 1 + GameServer()->Collision()->GetSwitchDelay(MapIndex) * Server()->TickSpeed();
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Type[Team()] = TILE_SWITCHTIMEDCLOSE;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SWITCHCLOSE && Team() != TEAM_SUPER && GameServer()->Collision()->GetSwitchNumber(MapIndex) > 0)
	{
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()] = false;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_EndTick[Team()] = 0;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Type[Team()] = TILE_SWITCHCLOSE;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_FREEZE && Team() != TEAM_SUPER)
	{
		if(GameServer()->Collision()->GetSwitchNumber(MapIndex) == 0 || GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()])
			Freeze(GameServer()->Collision()->GetSwitchDelay(MapIndex));
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_DFREEZE && Team() != TEAM_SUPER)
	{
		if(GameServer()->Collision()->GetSwitchNumber(MapIndex) == 0 || GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()])
			m_DeepFreeze = true;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_DUNFREEZE && Team() != TEAM_SUPER)
	{
		if(GameServer()->Collision()->GetSwitchNumber(MapIndex) == 0 || GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()])
			m_DeepFreeze = false;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_ENABLE && m_Hit & DISABLE_HIT_HAMMER && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_HAMMER)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can hammer hit others");
		m_Hit &= ~DISABLE_HIT_HAMMER;
		m_NeededFaketuning &= ~FAKETUNE_NOHAMMER;
		m_Core.m_NoHammerHit = false;
		GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_DISABLE && !(m_Hit & DISABLE_HIT_HAMMER) && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_HAMMER)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't hammer hit others");
		m_Hit |= DISABLE_HIT_HAMMER;
		m_NeededFaketuning |= FAKETUNE_NOHAMMER;
		m_Core.m_NoHammerHit = true;
		GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_ENABLE && m_Hit & DISABLE_HIT_SHOTGUN && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_SHOTGUN)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can shoot others with shotgun");
		m_Hit &= ~DISABLE_HIT_SHOTGUN;
		m_Core.m_NoShotgunHit = false;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_DISABLE && !(m_Hit & DISABLE_HIT_SHOTGUN) && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_SHOTGUN)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't shoot others with shotgun");
		m_Hit |= DISABLE_HIT_SHOTGUN;
		m_Core.m_NoShotgunHit = true;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_ENABLE && m_Hit & DISABLE_HIT_GRENADE && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_GRENADE)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can shoot others with grenade");
		m_Hit &= ~DISABLE_HIT_GRENADE;
		m_Core.m_NoGrenadeHit = false;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_DISABLE && !(m_Hit & DISABLE_HIT_GRENADE) && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_GRENADE)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't shoot others with grenade");
		m_Hit |= DISABLE_HIT_GRENADE;
		m_Core.m_NoGrenadeHit = true;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_ENABLE && m_Hit & DISABLE_HIT_LASER && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_LASER)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can shoot others with laser");
		m_Hit &= ~DISABLE_HIT_LASER;
		m_Core.m_NoLaserHit = false;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_DISABLE && !(m_Hit & DISABLE_HIT_LASER) && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_LASER)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't shoot others with laser");
		m_Hit |= DISABLE_HIT_LASER;
		m_Core.m_NoLaserHit = true;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_JUMP)
	{
		int newJumps = GameServer()->Collision()->GetSwitchDelay(MapIndex);

		if(newJumps != m_Core.m_Jumps)
		{
			char aBuf[256];
			if(newJumps == 1)
				str_format(aBuf, sizeof(aBuf), "You can jump %d time", newJumps);
			else
				str_format(aBuf, sizeof(aBuf), "You can jump %d times", newJumps);
			GameServer()->SendChatTarget(GetPlayer()->GetCID(), aBuf);

			if(newJumps == 0 && !m_SuperJump)
			{
				m_NeededFaketuning |= FAKETUNE_NOJUMP;
				GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
			}
			else if(m_Core.m_Jumps == 0)
			{
				m_NeededFaketuning &= ~FAKETUNE_NOJUMP;
				GameServer()->SendTuningParams(m_pPlayer->GetCID(), m_TuneZone); // update tunings
			}

			m_Core.m_Jumps = newJumps;
		}
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_ADD_TIME && !m_LastPenalty)
	{
		int min = GameServer()->Collision()->GetSwitchDelay(MapIndex);
		int sec = GameServer()->Collision()->GetSwitchNumber(MapIndex);
		int Team = Teams()->m_Core.Team(m_Core.m_Id);

		m_StartTime -= (min * 60 + sec) * Server()->TickSpeed();

		if((g_Config.m_SvTeam == 3 || Team != TEAM_FLOCK) && Team != TEAM_SUPER)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(Teams()->m_Core.Team(i) == Team && i != m_Core.m_Id && GameServer()->m_apPlayers[i])
				{
					CCharacter *pChar = GameServer()->m_apPlayers[i]->GetCharacter();

					if(pChar)
						pChar->m_StartTime = m_StartTime;
				}
			}
		}

		m_LastPenalty = true;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SUBSTRACT_TIME && !m_LastBonus)
	{
		int min = GameServer()->Collision()->GetSwitchDelay(MapIndex);
		int sec = GameServer()->Collision()->GetSwitchNumber(MapIndex);
		int Team = Teams()->m_Core.Team(m_Core.m_Id);

		m_StartTime += (min * 60 + sec) * Server()->TickSpeed();
		if(m_StartTime > Server()->Tick())
			m_StartTime = Server()->Tick();

		if((g_Config.m_SvTeam == 3 || Team != TEAM_FLOCK) && Team != TEAM_SUPER)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(Teams()->m_Core.Team(i) == Team && i != m_Core.m_Id && GameServer()->m_apPlayers[i])
				{
					CCharacter *pChar = GameServer()->m_apPlayers[i]->GetCharacter();

					if(pChar)
						pChar->m_StartTime = m_StartTime;
				}
			}
		}

		m_LastBonus = true;
	}

	if(GameServer()->Collision()->IsSwitch(MapIndex) != TILE_ADD_TIME)
	{
		m_LastPenalty = false;
	}

	if(GameServer()->Collision()->IsSwitch(MapIndex) != TILE_SUBSTRACT_TIME)
	{
		m_LastBonus = false;
	}

	int z = GameServer()->Collision()->IsTeleport(MapIndex);
	if(!g_Config.m_SvOldTeleportHook && !g_Config.m_SvOldTeleportWeapons && z && Controller->m_TeleOuts[z - 1].size())
	{
		if(m_Super)
			return;
		int TeleOut = m_Core.m_pWorld->RandomOr0(Controller->m_TeleOuts[z - 1].size());
		m_Core.m_Pos = Controller->m_TeleOuts[z - 1][TeleOut];
		if(!g_Config.m_SvTeleportHoldHook)
		{
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
			m_Core.m_HookPos = m_Core.m_Pos;
		}
		if(g_Config.m_SvTeleportLoseWeapons)
		{
			for(int i = WEAPON_SHOTGUN; i < NUM_WEAPONS - 1; i++)
				m_aWeapons[i].m_Got = false;
		}
		return;
	}
	int evilz = GameServer()->Collision()->IsEvilTeleport(MapIndex);
	if(evilz && Controller->m_TeleOuts[evilz - 1].size())
	{
		if(m_Super)
			return;
		int TeleOut = m_Core.m_pWorld->RandomOr0(Controller->m_TeleOuts[evilz - 1].size());
		m_Core.m_Pos = Controller->m_TeleOuts[evilz - 1][TeleOut];
		if(!g_Config.m_SvOldTeleportHook && !g_Config.m_SvOldTeleportWeapons)
		{
			m_Core.m_Vel = vec2(0, 0);

			if(!g_Config.m_SvTeleportHoldHook)
			{
				m_Core.m_HookedPlayer = -1;
				m_Core.m_HookState = HOOK_RETRACTED;
				m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
				GameWorld()->ReleaseHooked(GetPlayer()->GetCID());
				m_Core.m_HookPos = m_Core.m_Pos;
			}
			if(g_Config.m_SvTeleportLoseWeapons)
			{
				for(int i = WEAPON_SHOTGUN; i < NUM_WEAPONS - 1; i++)
					m_aWeapons[i].m_Got = false;
			}
		}
		return;
	}
	if(GameServer()->Collision()->IsCheckEvilTeleport(MapIndex))
	{
		if(m_Super)
			return;
		// first check if there is a TeleCheckOut for the current recorded checkpoint, if not check previous checkpoints
		for(int k = m_TeleCheckpoint - 1; k >= 0; k--)
		{
			if(Controller->m_TeleCheckOuts[k].size())
			{
				int TeleOut = m_Core.m_pWorld->RandomOr0(Controller->m_TeleCheckOuts[k].size());
				m_Core.m_Pos = Controller->m_TeleCheckOuts[k][TeleOut];
				m_Core.m_Vel = vec2(0, 0);

				if(!g_Config.m_SvTeleportHoldHook)
				{
					m_Core.m_HookedPlayer = -1;
					m_Core.m_HookState = HOOK_RETRACTED;
					m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
					GameWorld()->ReleaseHooked(GetPlayer()->GetCID());
					m_Core.m_HookPos = m_Core.m_Pos;
				}

				return;
			}
		}
		// if no checkpointout have been found (or if there no recorded checkpoint), teleport to start
		vec2 SpawnPos;
		if(GameServer()->m_pController->CanSpawn(m_pPlayer->GetTeam(), &SpawnPos))
		{
			m_Core.m_Pos = SpawnPos;
			m_Core.m_Vel = vec2(0, 0);

			if(!g_Config.m_SvTeleportHoldHook)
			{
				m_Core.m_HookedPlayer = -1;
				m_Core.m_HookState = HOOK_RETRACTED;
				m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
				GameWorld()->ReleaseHooked(GetPlayer()->GetCID());
				m_Core.m_HookPos = m_Core.m_Pos;
			}
		}
		return;
	}
	if(GameServer()->Collision()->IsCheckTeleport(MapIndex))
	{
		if(m_Super)
			return;
		// first check if there is a TeleCheckOut for the current recorded checkpoint, if not check previous checkpoints
		for(int k = m_TeleCheckpoint - 1; k >= 0; k--)
		{
			if(Controller->m_TeleCheckOuts[k].size())
			{
				int TeleOut = m_Core.m_pWorld->RandomOr0(Controller->m_TeleCheckOuts[k].size());
				m_Core.m_Pos = Controller->m_TeleCheckOuts[k][TeleOut];

				if(!g_Config.m_SvTeleportHoldHook)
				{
					m_Core.m_HookedPlayer = -1;
					m_Core.m_HookState = HOOK_RETRACTED;
					m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
					m_Core.m_HookPos = m_Core.m_Pos;
				}

				return;
			}
		}
		// if no checkpointout have been found (or if there no recorded checkpoint), teleport to start
		vec2 SpawnPos;
		if(GameServer()->m_pController->CanSpawn(m_pPlayer->GetTeam(), &SpawnPos))
		{
			m_Core.m_Pos = SpawnPos;

			if(!g_Config.m_SvTeleportHoldHook)
			{
				m_Core.m_HookedPlayer = -1;
				m_Core.m_HookState = HOOK_RETRACTED;
				m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
				m_Core.m_HookPos = m_Core.m_Pos;
			}
		}
		return;
	}
}

void CCharacter::HandleTuneLayer()
{
	m_TuneZoneOld = m_TuneZone;
	int CurrentIndex = GameServer()->Collision()->GetMapIndex(m_Pos);
	m_TuneZone = GameServer()->Collision()->IsTune(CurrentIndex);

	if(m_TuneZone)
		m_Core.m_pWorld->m_Tuning[g_Config.m_ClDummy] = GameServer()->TuningList()[m_TuneZone]; // throw tunings from specific zone into gamecore
	else
		m_Core.m_pWorld->m_Tuning[g_Config.m_ClDummy] = *GameServer()->Tuning();

	if(m_TuneZone != m_TuneZoneOld) // don't send tunigs all the time
	{
		// send zone msgs
		SendZoneMsgs();
	}
}

void CCharacter::SendZoneMsgs()
{
	// send zone leave msg
	// (m_TuneZoneOld >= 0: avoid zone leave msgs on spawn)
	if(m_TuneZoneOld >= 0 && GameServer()->m_aaZoneLeaveMsg[m_TuneZoneOld])
	{
		const char *pCur = GameServer()->m_aaZoneLeaveMsg[m_TuneZoneOld];
		const char *pPos;
		while((pPos = str_find(pCur, "\\n")))
		{
			char aBuf[256];
			str_copy(aBuf, pCur, pPos - pCur + 1);
			aBuf[pPos - pCur + 1] = '\0';
			pCur = pPos + 2;
			GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
		}
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), pCur);
	}
	// send zone enter msg
	if(GameServer()->m_aaZoneEnterMsg[m_TuneZone])
	{
		const char *pCur = GameServer()->m_aaZoneEnterMsg[m_TuneZone];
		const char *pPos;
		while((pPos = str_find(pCur, "\\n")))
		{
			char aBuf[256];
			str_copy(aBuf, pCur, pPos - pCur + 1);
			aBuf[pPos - pCur + 1] = '\0';
			pCur = pPos + 2;
			GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
		}
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), pCur);
	}
}

IAntibot *CCharacter::Antibot()
{
	return GameServer()->Antibot();
}

void CCharacter::SetRescue()
{
	m_RescueTee.save(this);
	m_SetSavePos = true;
}

void CCharacter::DDRaceTick()
{
	mem_copy(&m_Input, &m_SavedInput, sizeof(m_Input));
	//m_Armor=(m_FreezeTime >= 0)?10-(m_FreezeTime/15):0;
	if(m_Input.m_Direction != 0 || m_Input.m_Jump != 0)
		m_LastMove = Server()->Tick();

	if(m_FreezeTime > 0 || m_FreezeTime == -1)
	{
		if(m_FreezeTime % Server()->TickSpeed() == Server()->TickSpeed() - 1 || m_FreezeTime == -1)
		{
			GameServer()->CreateDamageInd(m_Pos, 0, (m_FreezeTime + 1) / Server()->TickSpeed(), Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		}
		if(m_FreezeTime > 0)
			m_FreezeTime--;
		else
			m_Ninja.m_ActivationTick = Server()->Tick();
		m_Input.m_Direction = 0;
		m_Input.m_Jump = 0;
		m_Input.m_Hook = 0;
		if(m_FreezeTime == 1)
			UnFreeze();
	}

	HandleTuneLayer(); // need this before coretick

	// look for save position for rescue feature
	if(g_Config.m_SvRescue || ((g_Config.m_SvTeam == 3 || Team() > TEAM_FLOCK) && Team() >= TEAM_FLOCK && Team() < TEAM_SUPER))
	{
		int index = GameServer()->Collision()->GetPureMapIndex(m_Pos);
		int tile = GameServer()->Collision()->GetTileIndex(index);
		int ftile = GameServer()->Collision()->GetFTileIndex(index);
		if(IsGrounded() && tile != TILE_FREEZE && tile != TILE_DFREEZE && ftile != TILE_FREEZE && ftile != TILE_DFREEZE && !m_DeepFreeze)
		{
			SetRescue();
		}
	}

	m_Core.m_Id = GetPlayer()->GetCID();
}

void CCharacter::DDRacePostCoreTick()
{
	m_Time = (float)(Server()->Tick() - m_StartTime) / ((float)Server()->TickSpeed());

	if(m_pPlayer->m_DefEmoteReset >= 0 && m_pPlayer->m_DefEmoteReset <= Server()->Tick())
	{
		m_pPlayer->m_DefEmoteReset = -1;
		m_EmoteType = m_pPlayer->m_DefEmote = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}

	if(m_EndlessHook || (m_Super && g_Config.m_SvEndlessSuperHook))
		m_Core.m_HookTick = 0;

	m_FrozenLastTick = false;

	if(m_DeepFreeze && !m_Super)
		Freeze();

	if(m_Core.m_Jumps == 0 && !m_Super)
		m_Core.m_Jumped = 3;
	else if(m_Core.m_Jumps == 1 && m_Core.m_Jumped > 0)
		m_Core.m_Jumped = 3;
	else if(m_Core.m_JumpedTotal < m_Core.m_Jumps - 1 && m_Core.m_Jumped > 1)
		m_Core.m_Jumped = 1;

	if((m_Super || m_SuperJump) && m_Core.m_Jumped > 1)
		m_Core.m_Jumped = 1;

	int CurrentIndex = GameServer()->Collision()->GetMapIndex(m_Pos);
	HandleSkippableTiles(CurrentIndex);
	if(!m_Alive)
		return;

	// handle Anti-Skip tiles
	std::list<int> Indices = GameServer()->Collision()->GetMapIndices(m_PrevPos, m_Pos);
	if(!Indices.empty())
	{
		for(std::list<int>::iterator i = Indices.begin(); i != Indices.end(); i++)
		{
			HandleTiles(*i);
			if(!m_Alive)
				return;
		}
	}
	else
	{
		HandleTiles(CurrentIndex);
		if(!m_Alive)
			return;
	}

	// teleport gun
	if(m_TeleGunTeleport)
	{
		GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID(), Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		m_Core.m_Pos = m_TeleGunPos;
		if(!m_IsBlueTeleGunTeleport)
			m_Core.m_Vel = vec2(0, 0);
		GameServer()->CreateDeath(m_TeleGunPos, m_pPlayer->GetCID(), Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		GameServer()->CreateSound(m_TeleGunPos, SOUND_WEAPON_SPAWN, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		m_TeleGunTeleport = false;
		m_IsBlueTeleGunTeleport = false;
	}

	HandleBroadcast();
}

bool CCharacter::Freeze(int Seconds)
{
	if((Seconds <= 0 || m_Super || m_FreezeTime == -1 || m_FreezeTime > Seconds * Server()->TickSpeed()) && Seconds != -1)
		return false;
	if(m_FreezeTick < Server()->Tick() - Server()->TickSpeed() || Seconds == -1)
	{
		m_Armor = 0;
		m_FreezeTime = Seconds == -1 ? Seconds : Seconds * Server()->TickSpeed();
		m_FreezeTick = Server()->Tick();
		return true;
	}
	return false;
}

bool CCharacter::Freeze()
{
	return Freeze(g_Config.m_SvFreezeDelay);
}

void CCharacter::GiveWeapon(int Weapon, int Ammo, bool Remove)
{
	if(Weapon == WEAPON_NINJA)
	{
		if(Remove)
			RemoveNinja();
		else
			GiveNinja();
		return;
	}

	if(Remove)
	{
		if(GetActiveWeapon() == Weapon)
			SetActiveWeapon(WEAPON_GUN);
	}
	else
	{
		m_aWeapons[Weapon].m_Ammo = Ammo;
	}

	m_aWeapons[Weapon].m_Got = !Remove;
}

void CCharacter::GiveAllWeapons()
{
	for(int i = WEAPON_GUN; i < NUM_WEAPONS - 1; i++)
	{
		GiveWeapon(i, 10);
	}
}

void CCharacter::Pause(bool Pause)
{
	m_Paused = Pause;
	if(Pause)
	{
		GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
		GameServer()->m_World.RemoveEntity(this);

		if(m_Core.m_HookedPlayer != -1) // Keeping hook would allow cheats
		{
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
		}
	}
	else
	{
		m_Core.m_Vel = vec2(0, 0);
		GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;
		GameServer()->m_World.InsertEntity(this);
	}
}

void CCharacter::DDRaceInit()
{
	m_Paused = false;
	m_DDRaceState = DDRACE_NONE;
	m_PrevPos = m_Pos;
	m_SetSavePos = false;
	m_LastBroadcast = 0;
	m_TeamBeforeSuper = 0;
	m_Core.m_Id = GetPlayer()->GetCID();
	m_TeleCheckpoint = 0;
	m_EndlessHook = g_Config.m_SvEndlessDrag;
	m_Hit = g_Config.m_SvHit ? HIT_ALL : DISABLE_HIT_GRENADE | DISABLE_HIT_HAMMER | DISABLE_HIT_LASER | DISABLE_HIT_SHOTGUN;
	m_SuperJump = false;
	m_Jetpack = false;
	m_Core.m_Jumps = 2;
	m_FreezeHammer = false;

	int Team = Teams()->m_Core.Team(m_Core.m_Id);

	if(Teams()->TeamLocked(Team))
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(Teams()->m_Core.Team(i) == Team && i != m_Core.m_Id && GameServer()->m_apPlayers[i])
			{
				CCharacter *pChar = GameServer()->m_apPlayers[i]->GetCharacter();

				if(pChar)
				{
					m_DDRaceState = pChar->m_DDRaceState;
					m_StartTime = pChar->m_StartTime;
				}
			}
		}
	}

	if(g_Config.m_SvTeam == 2 && Team == TEAM_FLOCK)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Please join a team before you start");
		m_LastStartWarning = Server()->Tick();
	}
}

void CCharacter::Rescue()
{
	if(m_SetSavePos && !m_Super)
	{
		if(m_LastRescue + g_Config.m_SvRescueDelay * Server()->TickSpeed() > Server()->Tick())
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "You have to wait %d seconds until you can rescue yourself", (int)((m_LastRescue + g_Config.m_SvRescueDelay * Server()->TickSpeed() - Server()->Tick()) / Server()->TickSpeed()));
			GameServer()->SendChatTarget(GetPlayer()->GetCID(), aBuf);
			return;
		}

		float StartTime = m_StartTime;
		m_RescueTee.load(this, Team());
		// Don't load these from saved tee:
		m_Core.m_Vel = vec2(0, 0);
		m_Core.m_HookState = HOOK_IDLE;
		m_StartTime = StartTime;
		m_SavedInput.m_Direction = 0;
		m_SavedInput.m_Jump = 0;
		// simulate releasing the fire button
		if((m_SavedInput.m_Fire & 1) != 0)
			m_SavedInput.m_Fire++;
		m_SavedInput.m_Fire &= INPUT_STATE_MASK;
		m_SavedInput.m_Hook = 0;
		m_pPlayer->Pause(CPlayer::PAUSE_NONE, true);
	}
}

// riding a taxi
bool CCharacter::IsTaxiPassenger()
{
	if (m_Core.m_TaxiDriverCore)
	  return true;
	return false;
}

// carrying a passenger right now
bool CCharacter::IsTaxiDriver()
{
	if (m_Core.m_TaxiPassengerCore)
	  return true;
	return false;
}

void CCharacter::ExitTaxiAsPassenger()
{
	// passenger exits
	m_Core.m_Pos.x = m_Core.m_TaxiDriverCore->m_Pos.x;
	m_Core.m_Pos.y = m_Core.m_TaxiDriverCore->m_Pos.y;
	m_Core.m_Vel.x = m_Core.m_TaxiDriverCore->m_Vel.x;
	m_Core.m_Vel.y = m_Core.m_TaxiDriverCore->m_Vel.y;
	// driver forgets about a passenger
	m_Core.m_TaxiDriverCore->m_TaxiPassengerCore = nullptr;
	// we forget about a driver
	m_Core.m_TaxiDriverCore = nullptr;
	// set out status
	m_TaxiPassenger = false;
}

void CCharacter::ExitTaxiAsDriver()
{
	// passenger exits
	m_Core.m_TaxiPassengerCore->m_Pos.x = m_Core.m_Pos.x;
	m_Core.m_TaxiPassengerCore->m_Pos.y = m_Core.m_Pos.y;
	m_Core.m_TaxiPassengerCore->m_Vel.x = m_Core.m_Vel.x;
	m_Core.m_TaxiPassengerCore->m_Vel.y = m_Core.m_Vel.y;
	// passengers forgets about a driver
	m_Core.m_TaxiPassengerCore->m_TaxiDriverCore = nullptr;
	// we forget about a passenger
	m_Core.m_TaxiPassengerCore = nullptr;
}


// INFCROYA BEGIN ------------------------------------------------------------
void CCharacter::ResetTaxi()
{
	m_FreeTaxi = false;
	if (IsTaxiPassenger()) {
		ExitTaxiAsPassenger();
	}
	if (IsTaxiDriver()) {
		ExitTaxiAsDriver();
	}
}

int CCharacter::VacantBotId() {
	for(int i = 51; i < MAX_CLIENTS; i++)
	{
		if (!GameServer()->m_apPlayers[i])
			return i;
	}
	return -1;

}

void CCharacter::DestroyMyBot() {
	if (m_IsBot)
		return;
	if (m_BotClientID < 0)
		return;
	DestroyBotByID(m_BotClientID);
	m_BotClientID = -1;
}

void CCharacter::DestroyAllBots() {
	for(int i = 51; i < MAX_CLIENTS; i++)
	{
		CPlayer *player = GameServer()->m_apPlayers[i];
		if (!player)
		  continue;
		if (!player->IsBot())
		  continue;
		GameServer()->m_apPlayers[i] = nullptr;
		delete player;
	}
}

void CCharacter::DestroyBotByID(int BotID) {
	if (BotID < 1)
		return;
	CPlayer *player = GameServer()->m_apPlayers[BotID];
	if (!player)
	  return;
	if (!player->IsBot())
	  return;
	GameServer()->m_apPlayers[BotID] = nullptr;
	delete player;
}

void CCharacter::RegainBotControl() {
	if (IsHuman())
		return;
	if (m_BotClientID < 0)
		return;
	CPlayer *player = GameServer()->m_apPlayers[m_BotClientID];
	if (!player)
		return;
	if (!player->IsBot())
		return;
	if (!player->m_pCharacter)
		return;
	CCharacter *m_pBotCharacter = player->m_pCharacter;
	if (!m_pBotCharacter)
		return;
	if (!m_pBotCharacter->GetpCore())
		return;
	m_pBotCharacter->GetpCore()->m_IsBot = true;
	m_pBotCharacter->GetpCore()->m_BotOwnerCore = &m_Core;
}

void CCharacter::SpawnBot() {
	if (IsHuman())
		return;
	if (m_BotClientID > -1)
		return;
	int ClientID = VacantBotId();
	m_BotClientID = ClientID;
	if (ClientID < 0)
		return;
	auto mod = GameServer()->m_pController;
	auto pPlayer = new(ClientID) CPlayer(GameServer(), ClientID, 0);
	pPlayer->m_Afk = false;
	pPlayer->m_IsBot = true;
	GameServer()->m_apPlayers[ClientID] = pPlayer;
	auto croyaPlayer = new CroyaPlayer(ClientID, pPlayer, GameServer(), mod, mod->classes);
	mod->players[ClientID] = croyaPlayer;
	// auto m_pBotCharacter = new(ClientID) CCharacter(&GameServer()->m_World);
	auto m_pBotCharacter = pPlayer->ForceSpawn(m_Pos);
	m_pBotCharacter->m_IsBot = true;
	pPlayer->m_pCharacter = m_pBotCharacter;
	// maybe we don't need character core
	pPlayer->m_BotOwnerCore = &m_Core;
	m_pBotCharacter->GetpCore()->m_IsBot = true;
	m_pBotCharacter->GetpCore()->m_BotOwnerCore = &m_Core;
	croyaPlayer->SetClassNum(WORKER);
	//GameServer()->OnClientEnter(ClientID);
}

void CCharacter::SwitchTaxi()
{
	// we have a free taxi icon so we don't carrying or riding anybody
	if (m_FreeTaxi)
		m_FreeTaxi = false;
	else {
		// we don't have a free taxi sign
		// either we haven't turned it on
		// or we are a passenger or a driver

		// let's get rid of passenger or driver first
		if (IsTaxiPassenger()) {
			ExitTaxiAsPassenger();
			return; // we don't want a taxi icon if we exited
		}
		if (IsTaxiDriver()) {
			ExitTaxiAsDriver();
			return; // we don't want a taxi icon if we stopped driving
		}
		m_FreeTaxi = true;
		GetCroyaPlayer()->SetHookProtected(false);
	}
}

void CCharacter::SetNormalEmote(int Emote)
{
	m_NormalEmote = Emote;
}

bool CCharacter::IsHuman() const {
	return !m_Infected;
}

bool CCharacter::IsZombie() const {
	return m_Infected;
}

void CCharacter::SetInfected(bool Infected) {
	m_Infected = Infected;
	m_Core.m_Infected = Infected;
}

void CCharacter::SetCroyaPlayer(CroyaPlayer* CroyaPlayer) {
	m_pCroyaPlayer = CroyaPlayer;
}

CroyaPlayer* CCharacter::GetCroyaPlayer() {
	return m_pCroyaPlayer;
}

void CCharacter::ResetWeaponsHealth()
{
	m_Health = 1;
	m_Armor = 0;
	for (auto& each : m_aWeapons) {
		each.m_Got = false;
	}
}

//int CCharacter::GetActiveWeapon() const
//{
//	return m_ActiveWeapon;
//}

void CCharacter::SetReloadTimer(int ReloadTimer)
{
	m_ReloadTimer = ReloadTimer;
}

void CCharacter::SetNumObjectsHit(int NumObjectsHit)
{
	m_NumObjectsHit = NumObjectsHit;
}

// by a hammer or stepping into inf zone
void CCharacter::Infect(int From)
{
	if (GameServer()->m_pController->IsWarmup())
	  return;
	if (From >= 0) { // -1 and below is a special case (e.g infect when inside infection zone)
		// Kill message (copypasted from CCharacter::TakeDamage)
		CNetMsg_Sv_KillMsg Msg;
		Msg.m_Killer = From;
		Msg.m_Victim = m_pPlayer->GetCID();
		Msg.m_Weapon = WEAPON_HAMMER;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

		// BEGIN // do that before you actually turn someone into a zombie
		GetCroyaPlayer()->SetOldClassNum(GetCroyaPlayer()->GetClassNum());
		GameServer()->m_apPlayers[From]->GetCroyaPlayer()->OnKill(GetPlayer()->GetCID());
		// END //   do that before you actually turn someone into a zombie
	}
	GetCroyaPlayer()->SetOldClassNum(GetCroyaPlayer()->GetClassNum());
	GetCroyaPlayer()->TurnIntoRandomZombie();
}

bool CCharacter::IncreaseOverallHp(int Amount)
{
	bool success = false;
	if (m_Health < 10)
	{
		int healthDiff = 10 - m_Health;
		IncreaseHealth(Amount);
		success = true;
		Amount = Amount - healthDiff;
	}
	if (Amount > 0)
	{
		if (IncreaseArmor(Amount))
			success = true;
	}
	return success;
}

int CCharacter::GetArmor() const
{
	return m_Armor;
}

int CCharacter::GetHealthArmorSum() const
{
	return m_Health + m_Armor;
}

void CCharacter::SetHealthArmor(int Health, int Armor)
{
	m_Health = Health;
	m_Armor = Armor;
}

CCharacterCore& CCharacter::GetCharacterCore()
{
	return m_Core;
}

void CCharacter::Stun(float Time, float Immunity)
{
	m_Immunity = Immunity;
	if (Stunned() || Frozen())
		return;

	if(GetCroyaPlayer()->GetClassNum() == Class::SCIENTIST ||
	   GetCroyaPlayer()->GetClassNum() == Class::NINJA ||
	   GetCroyaPlayer()->GetClassNum() == Class::PSYCHO) {
		return;
	}

	// immunity for 4 seconds after last stun
	if (m_StunTime > -1 && ((Server()->Tick() - m_StunTime) / Server()->TickSpeed() < m_Immunity))
		return;

	CCharacterCore& Core = GetCharacterCore();
	Core.m_HookedPlayer = -1;
	Core.m_HookState = HOOK_RETRACTED;
	Core.m_HookPos = Core.m_Pos;

	m_IsStunned = true;
	m_StunTime = Server()->Tick() + (Server()->TickSpeed() * Time); // untun time in fact
	//m_FrozenTime = Server()->TickSpeed() * Time;

	GiveNinja();
}

/* void CCharacter::Unfreeze()
{
	if (!m_IsFrozen) {
		return;
	}
	m_IsFrozen = false;
	//m_FrozenTime = -1;

	//if (m_FreezeReason == FREEZEREASON_UNDEAD)
	//{
	m_Health = 10.0;
	//}

	GameServer()->CreatePlayerSpawn(m_Pos);
} */

bool CCharacter::Frozen() {
	if (m_FreezeTime <= 0)
	  return false;
	return true;
}

bool CCharacter::Stunned() {
	return m_IsStunned;
/* 	if (m_StunTime <= 0)
	  return false;
	return true; */
}

bool CCharacter::UnFreeze()
{
	if (!Frozen())
	  return false;

	m_Health = 10;
	m_Armor=0;
	if(!m_aWeapons[m_Core.m_ActiveWeapon].m_Got)
		m_Core.m_ActiveWeapon = WEAPON_HAMMER;
	if(!m_aWeapons[m_Core.m_ActiveWeapon].m_Got)
		m_Core.m_ActiveWeapon = WEAPON_GUN;
	m_FreezeTime = 0;
	m_FreezeTick = 0;
	m_FrozenLastTick = true;
	return true;
}

void CCharacter::Poison(int Count, int From)
{
	if (m_Poison <= 0)
	{
		m_PoisonTick = 0;
		m_Poison = Count;
		m_PoisonFrom = From;
	}
}

void CCharacter::DestroyChildEntities()
{
	DestroyMyBot();
	CGameWorld *gw = GameWorld();
	if (!gw)
		return;
	for (CEngineerWall* pWall = (CEngineerWall*)GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*)pWall->TypeNext())
	{
		if (pWall)
		{
			if (pWall->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pWall);
		}
	}
	for (CLooperWall* pWall = (CLooperWall*)GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*)pWall->TypeNext())
	{
		if (pWall)
		{
			if (pWall->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pWall);
		}
	}
	for (CSoldierBomb* pBomb = (CSoldierBomb*)GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb*)pBomb->TypeNext())
	{
		if (pBomb)
		{
			if (pBomb->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pBomb);
		}
	}
	for (CScientistMine* pMine = (CScientistMine*)GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE); pMine; pMine = (CScientistMine*)pMine->TypeNext())
	{
		if (pMine)
		{
			if (pMine->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pMine);
		}
	}
	for (CBiologistMine* pMine = (CBiologistMine*)GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*)pMine->TypeNext())
	{
		if (pMine)
		{
			if (pMine->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pMine);
		}
	}
	for (CMedicGrenade* pGrenade = (CMedicGrenade*)GameWorld()->FindFirst(CGameWorld::ENTTYPE_MEDIC_GRENADE); pGrenade; pGrenade = (CMedicGrenade*)pGrenade->TypeNext())
	{
		if (pGrenade)
		{
			if (pGrenade->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pGrenade);
		}
	}
	for (CMercenaryBomb* pBomb = (CMercenaryBomb*)GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*)pBomb->TypeNext())
	{
		if (pBomb)
		{
			if (pBomb->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pBomb);
		}
	}
	for (CScatterGrenade* pGrenade = (CScatterGrenade*)GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCATTER_GRENADE); pGrenade; pGrenade = (CScatterGrenade*)pGrenade->TypeNext())
	{
		if (pGrenade)
		{
			if (pGrenade->m_Owner != m_pPlayer->GetCID()) continue;
			GameServer()->m_World.DestroyEntity(pGrenade);
		}
	}

	// engineer wall first dot
	m_FirstShot = true;
}

bool CCharacter::IsHookProtected() const
{
	return m_HookProtected;
}

void CCharacter::SetHookProtected(bool HookProtected)
{
	m_HookProtected = HookProtected;
	m_Core.m_HookProtected = HookProtected;
}

CNetObj_PlayerInput& CCharacter::GetInput()
{
	return m_Input;
}

bool CCharacter::FindPortalPosition(vec2 Pos, vec2& Res)
{
	vec2 PortalShift = Pos - m_Pos;
	vec2 PortalDir = normalize(PortalShift);
	if (length(PortalShift) > 500.0f)
		PortalShift = PortalDir * 500.0f;

	float Iterator = length(PortalShift);
	while (Iterator > 0.0f)
	{
		PortalShift = PortalDir * Iterator;
		vec2 PortalPos = m_Pos + PortalShift;

		if (GameServer()->m_pController->IsSpawnable(PortalPos))
		{
			Res = PortalPos;
			return true;
		}

		Iterator -= 4.0f;
	}

	return false;
}

void CCharacter::SaturateVelocity(vec2 Force, float MaxSpeed)
{
	if (length(Force) < 0.00001)
		return;

	float Speed = length(m_Core.m_Vel);
	vec2 VelDir = normalize(m_Core.m_Vel);
	if (Speed < 0.00001)
	{
		VelDir = normalize(Force);
	}
	vec2 OrthoVelDir = vec2(-VelDir.y, VelDir.x);
	float VelDirFactor = dot(Force, VelDir);
	float OrthoVelDirFactor = dot(Force, OrthoVelDir);

	vec2 NewVel = m_Core.m_Vel;
	if (Speed < MaxSpeed || VelDirFactor < 0.0f)
	{
		NewVel += VelDir * VelDirFactor;
		float NewSpeed = length(NewVel);
		if (NewSpeed > MaxSpeed)
		{
			if (VelDirFactor > 0.f)
				NewVel = VelDir * MaxSpeed;
			else
				NewVel = -VelDir * MaxSpeed;
		}
	}

	NewVel += OrthoVelDir * OrthoVelDirFactor;

	m_Core.m_Vel = NewVel;
}

int CCharacter::GetInfWeaponID(int WID)
{
	if (WID == WEAPON_HAMMER)
	{
	}
	else if (WID == WEAPON_GUN)
	{
		switch (GetCroyaPlayer()->GetClassNum())
		{
		case Class::MERCENARY:
			return INFWEAPON_MERCENARY_GUN;
		default:
			return INFWEAPON_GUN;
		}
		return INFWEAPON_GUN;
	}
	else if (WID == WEAPON_SHOTGUN)
	{
		switch (GetCroyaPlayer()->GetClassNum())
		{
		case Class::MEDIC:
			return INFWEAPON_MEDIC_SHOTGUN;
		case Class::BIOLOGIST:
			return INFWEAPON_BIOLOGIST_SHOTGUN;
		case Class::HERO:
			return INFWEAPON_HERO_SHOTGUN;
		default:
			return INFWEAPON_SHOTGUN;
		}
	}
	else if (WID == WEAPON_GRENADE)
	{
		switch (GetCroyaPlayer()->GetClassNum())
		{
		case Class::MERCENARY:
			return INFWEAPON_MERCENARY_GRENADE;
		case Class::MEDIC:
			return INFWEAPON_MEDIC_GRENADE;
		case Class::SOLDIER:
			return INFWEAPON_SOLDIER_GRENADE;
		case Class::MAGICIAN:
			return INFWEAPON_MAGICIAN_GRENADE;
		case Class::HERO:
			return INFWEAPON_HERO_GRENADE;
		case Class::SCIENTIST:
			return INFWEAPON_SCIENTIST_GRENADE;
		default:
			return INFWEAPON_GRENADE;
		}
	}
	else if (WID == WEAPON_LASER)
	{
		switch (GetCroyaPlayer()->GetClassNum())
		{
		case Class::LOOPER:
			return INFWEAPON_LOOPER_RIFLE;
		case Class::ENGINEER:
			return INFWEAPON_ENGINEER_RIFLE;
		case Class::SCIENTIST:
			return INFWEAPON_SCIENTIST_RIFLE;
		case Class::BIOLOGIST:
			return INFWEAPON_BIOLOGIST_RIFLE;
		case Class::MAGICIAN:
			return INFWEAPON_MAGICIAN_RIFLE;
		case Class::MEDIC:
			return INFWEAPON_MEDIC_RIFLE;
		case Class::SNIPER:
			return INFWEAPON_SNIPER_RIFLE;
		default:
			return INFWEAPON_RIFLE;
		}
	}
	else if (WID == WEAPON_NINJA)
	{
		return INFWEAPON_NINJA;
	}
	return INFWEAPON_NONE;
}
int CCharacter::GetLastNoAmmoSound() const
{
	return m_LastNoAmmoSound;
}
void CCharacter::SetLastNoAmmoSound(int LastNoAmmoSound)
{
	m_LastNoAmmoSound = LastNoAmmoSound;
}

int CCharacter::GetLastNoAttachSound() const
{
	return m_LastNoAttachSound;
}
void CCharacter::SetLastNoAttachSound(int LastNoAttachSound)
{
	m_LastNoAttachSound = LastNoAttachSound;
}
// INFCROYA END ------------------------------------------------------------//

void CCharacter::SlowMotionEffect(float duration)
{
	if (duration == 0) return;
	duration *= 0.1f;
	if(m_SlowMotionTick <= 0)
	{
		m_SlowMotionTick = Server()->TickSpeed()*duration;
		m_IsInSlowMotion = true;
		m_Core.m_Vel *= 0.4;
	}
}

bool CCharacter::IsInSlowMotion() const
{
	return m_SlowMotionTick > 0;
}
