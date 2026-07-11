/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_GMWATCHHELPER_H
#define PLAYERBOTS_GMWATCHHELPER_H

class ChatHandler;
class Player;

namespace GmWatchHelper
{
    bool BeginWatch(ChatHandler* handler, Player* observer, Player* target);
    bool BeginTakeover(ChatHandler* handler, Player* observer, Player* target);
    void EndWatch(Player* observer, bool returnToEntry);
    void Update(Player* observer);
}

#endif
