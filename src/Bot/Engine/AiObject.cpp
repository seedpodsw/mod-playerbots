/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AiObject.h"

#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

AiObject::AiObject(PlayerbotAI* botAI)
    : PlayerbotAIAware(botAI), bot(botAI ? botAI->GetBot() : nullptr), context(botAI->GetAiObjectContext()), chat(botAI->GetChatHelper())
{
}

void AiObject::RefreshBot()
{
    if (botAI)
        bot = botAI->GetBot();
}

Player* AiObject::GetValidBot() const
{
    if (!botAI)
        return nullptr;

    Player* activeBot = botAI->GetBot();
    if (!activeBot || !activeBot->GetSession() || !activeBot->IsInWorld() ||
        activeBot->IsDuringRemoveFromWorld())
        return nullptr;

    return activeBot;
}

Player* AiObject::GetValidMaster() const
{
    if (!botAI)
        return nullptr;

    return const_cast<PlayerbotAI*>(botAI)->GetValidMaster();
}

Player* AiObject::GetMaster() { return botAI->GetMaster(); }
