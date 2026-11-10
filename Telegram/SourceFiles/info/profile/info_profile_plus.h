/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Info {
namespace Profile {

// TelegramPlus: identifiers and technical details shown in every profile.

// The id the way Telegram API uses it: 123456789.
[[nodiscard]] QString PlusPeerIdText(not_null<PeerData*> peer);

// The id the way Bot API uses it: -123456789 for basic groups and
// -1001234567890 for supergroups and channels. Empty for users, their id
// is the same in both APIs.
[[nodiscard]] QString PlusBotApiIdText(not_null<PeerData*> peer);

// Type, data center, flags, dates and linked chats of the peer, one fact
// per line. Updated when the information about the peer changes.
[[nodiscard]] rpl::producer<TextWithEntities> PlusDetailsValue(
	not_null<PeerData*> peer);

} // namespace Profile
} // namespace Info
