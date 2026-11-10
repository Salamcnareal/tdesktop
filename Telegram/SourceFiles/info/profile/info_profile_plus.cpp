/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_plus.h"

#include "base/unixtime.h"
#include "countries/countries_instance.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/image/image_location.h"

namespace Info {
namespace Profile {
namespace {

// Bot API shifts supergroup and channel ids by this value: -(10^12 + id).
constexpr auto kBotApiChannelShift = 1'000'000'000'000LL;

[[nodiscard]] qint64 RawPeerId(not_null<PeerData*> peer) {
	return qint64(peer->id.value & PeerId::kChatTypeMask);
}

[[nodiscard]] QString TypeText(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return user->isInaccessible()
			? u"Deleted account"_q
			: user->isBot()
			? u"Bot"_q
			: user->isSelf()
			? u"User (you)"_q
			: u"User"_q;
	} else if (peer->isChat()) {
		return u"Group (basic)"_q;
	} else if (const auto channel = peer->asChannel()) {
		return channel->isCommunity()
			? u"Community"_q
			: channel->isMonoforum()
			? u"Channel direct messages"_q
			: channel->isForum()
			? u"Supergroup (forum)"_q
			: channel->isGigagroup()
			? u"Broadcast group"_q
			: channel->isMegagroup()
			? u"Supergroup"_q
			: u"Channel"_q;
	}
	return QString();
}

// The data center is known only from the profile photo location, so for
// a peer without a photo it stays unknown.
[[nodiscard]] QString DataCenterText(not_null<PeerData*> peer) {
	const auto location = peer->userpicLocation();
	const auto storage = std::get_if<StorageFileLocation>(
		&location.file().data);
	const auto dcId = storage ? int(storage->dcId()) : 0;
	if (dcId <= 0) {
		return QString();
	}
	const auto place = [&] {
		switch (dcId) {
		case 1: return u"Miami, USA"_q;
		case 2: return u"Amsterdam, NL"_q;
		case 3: return u"Miami, USA"_q;
		case 4: return u"Amsterdam, NL"_q;
		case 5: return u"Singapore, SG"_q;
		}
		return QString();
	}();
	return place.isEmpty()
		? QString::number(dcId)
		: (QString::number(dcId) + u" ("_q + place + u")"_q);
}

[[nodiscard]] QStringList FlagsList(not_null<PeerData*> peer) {
	auto result = QStringList();
	const auto add = [&](bool condition, const QString &text) {
		if (condition) {
			result.push_back(text);
		}
	};
	add(peer->isVerified(), u"verified"_q);
	add(peer->isPremium(), u"premium"_q);
	add(peer->isScam(), u"scam"_q);
	add(peer->isFake(), u"fake"_q);
	add(peer->isBlocked(), u"blocked by you"_q);
	if (const auto user = peer->asUser()) {
		add(user->isSupport(), u"support"_q);
		add(user->isContact(), u"contact"_q);
		if (const auto info = user->botInfo.get()) {
			add(info->readsAllHistory, u"reads all group messages"_q);
			add(info->cantJoinGroups, u"can't join groups"_q);
			add(!info->inlinePlaceholder.isEmpty(), u"inline"_q);
			add(info->supportsBusiness, u"business"_q);
			add(info->hasMainApp, u"has mini app"_q);
		}
	} else if (const auto chat = peer->asChat()) {
		add(chat->amCreator(), u"you are the creator"_q);
		add(chat->isDeactivated(), u"deactivated"_q);
	} else if (const auto channel = peer->asChannel()) {
		add(true, channel->isPublic() ? u"public"_q : u"private"_q);
		add(channel->amCreator(), u"you are the creator"_q);
	}
	return result;
}

[[nodiscard]] QString DateText(TimeId date) {
	return langDateTimeFull(base::unixtime::parse(date));
}

[[nodiscard]] QString ComputeDetails(not_null<PeerData*> peer) {
	auto lines = QStringList();
	const auto add = [&](const QString &label, const QString &value) {
		if (!value.isEmpty()) {
			lines.push_back(label + u": "_q + value);
		}
	};
	add(u"Type"_q, TypeText(peer));
	add(u"Data center"_q, DataCenterText(peer));
	add(u"Flags"_q, FlagsList(peer).join(u", "_q));
	if (const auto user = peer->asUser()) {
		if (const auto info = user->botInfo.get()) {
			if (info->activeUsers > 0) {
				add(u"Monthly users"_q, QString::number(info->activeUsers));
			}
		}
		const auto month = user->registrationMonth();
		const auto year = user->registrationYear();
		if (month && year) {
			add(u"Registered"_q, langMonthOfYearFull(month, year));
		}
		const auto country = user->phoneCountryCode();
		if (!country.isEmpty()) {
			const auto name = Countries::Instance().countryNameByISO2(
				country);
			add(
				u"Phone country"_q,
				name.isEmpty() ? country : (name + u" ("_q + country + ')'));
		}
	} else if (const auto chat = peer->asChat()) {
		if (chat->date) {
			add(u"Created"_q, DateText(chat->date));
		}
		if (const auto to = chat->migrateTo()) {
			add(u"Migrated to"_q, PlusBotApiIdText(to));
		}
	} else if (const auto channel = peer->asChannel()) {
		// The server reports a single date: when you joined the chat if you
		// are in it (so the creation date for its creator), the creation
		// date otherwise. The invite date is the more exact one for joins.
		const auto joined = channel->amIn() && !channel->amCreator();
		const auto date = (joined && channel->inviteDate)
			? channel->inviteDate
			: TimeId(channel->date);
		if (date) {
			add(joined ? u"You joined"_q : u"Created"_q, DateText(date));
		}
		if (const auto linked = channel->discussionLink()) {
			add(
				(channel->isBroadcast()
					? u"Discussion group"_q
					: u"Linked channel"_q),
				PlusBotApiIdText(linked));
		}
		if (const auto from = channel->migrateFrom()) {
			add(u"Migrated from"_q, PlusBotApiIdText(from));
		}
	}
	return lines.join('\n');
}

} // namespace

QString PlusPeerIdText(not_null<PeerData*> peer) {
	return QString::number(RawPeerId(peer));
}

QString PlusBotApiIdText(not_null<PeerData*> peer) {
	if (peer->isChat()) {
		return QString::number(-RawPeerId(peer));
	} else if (peer->isChannel()) {
		return QString::number(-(kBotApiChannelShift + RawPeerId(peer)));
	}
	return QString();
}

rpl::producer<TextWithEntities> PlusDetailsValue(not_null<PeerData*> peer) {
	using Flag = Data::PeerUpdate::Flag;
	return peer->session().changes().peerFlagsValue(
		peer,
		(Flag::Name
			| Flag::Photo
			| Flag::FullInfo
			| Flag::IsBlocked
			| Flag::IsContact
			| Flag::IsBot
			| Flag::EmojiStatus
			| Flag::VerifyInfo
			| Flag::Migration
			| Flag::BarSettings
			| Flag::Rights
			| Flag::ChannelAmIn
			| Flag::DiscussionLink)
	) | rpl::map([=] {
		return TextWithEntities{ ComputeDetails(peer) };
	}) | rpl::distinct_until_changed();
}

} // namespace Profile
} // namespace Info
