/*
 * Copyright (C) 2020 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Migration.hpp"

#include "services/database/Db.hpp"
#include "services/database/ScanSettings.hpp"
#include "services/database/Session.hpp"
#include "services/database/User.hpp"
#include "utils/Exception.hpp"
#include "utils/Logger.hpp"

namespace Database
{
	VersionInfo::pointer
	VersionInfo::getOrCreate(Session& session)
	{
		session.checkUniqueLocked();

		pointer versionInfo {session.getDboSession().find<VersionInfo>()};
		if (!versionInfo)
			return session.getDboSession().add(std::make_unique<VersionInfo>());

		return versionInfo;
	}

	VersionInfo::pointer
	VersionInfo::get(Session& session)
	{
		session.checkSharedLocked();

		return session.getDboSession().find<VersionInfo>();
	}

}

namespace Database::Migration
{
	class ScopedNoForeignKeys
	{
		public:
			ScopedNoForeignKeys(Db& db) : _db {db}
			{
				_db.executeSql("PRAGMA foreign_keys=OFF");
			}
			~ScopedNoForeignKeys()
			{
				_db.executeSql("PRAGMA foreign_keys=ON");
			}

			ScopedNoForeignKeys(const ScopedNoForeignKeys&) = delete;
			ScopedNoForeignKeys(ScopedNoForeignKeys&&) = delete;
			ScopedNoForeignKeys& operator=(const ScopedNoForeignKeys&) = delete;
			ScopedNoForeignKeys& operator=(ScopedNoForeignKeys&&) = delete;

		private:
			Db& _db;
	};

	void
	doDbMigration(Session& session)
	{
		static const std::string outdatedMsg {"Outdated database, please rebuild it (delete the .db file and restart)"};

		ScopedNoForeignKeys noPragmaKeys {session.getDb()};

		while (1)
		{
			auto uniqueTransaction {session.createUniqueTransaction()};

			Version version;
			try
			{
				version = VersionInfo::getOrCreate(session)->getVersion();
				LMS_LOG(DB, INFO) << "Database version = " << version << ", LMS binary version = " << LMS_DATABASE_VERSION;
				if (version == LMS_DATABASE_VERSION)
				{
					LMS_LOG(DB, DEBUG) << "Lms database version " << LMS_DATABASE_VERSION << ": up to date!";
					return;
				}
			}
			catch (std::exception& e)
			{
				LMS_LOG(DB, ERROR) << "Cannot get database version info: " << e.what();
				throw LmsException {outdatedMsg};
			}

			LMS_LOG(DB, INFO) << "Migrating database from version " << version << "...";

			if (version == 5)
			{
				session.getDboSession().execute("DELETE FROM auth_token"); // format has changed
			}
			else if (version == 6)
			{
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 7)
			{
				session.getDboSession().execute("DROP TABLE similarity_settings");
				session.getDboSession().execute("DROP TABLE similarity_settings_feature");
				session.getDboSession().execute("ALTER TABLE scan_settings ADD similarity_engine_type INTEGER NOT NULL DEFAULT(" + std::to_string(static_cast<int>(ScanSettings::RecommendationEngineType::Clusters)) + ")");
			}
			else if (version == 8)
			{
				// Better cover handling, need to rescan the whole files
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 9)
			{
				session.getDboSession().execute(R"(
CREATE TABLE IF NOT EXISTS "track_bookmark" (
	"id" integer primary key autoincrement,
	"version" integer not null,
	"offset" integer,
	"comment" text not null,
	"track_id" bigint,
	"user_id" bigint,
	constraint "fk_track_bookmark_track" foreign key ("track_id") references "track" ("id") on delete cascade deferrable initially deferred,
	constraint "fk_track_bookmark_user" foreign key ("user_id") references "user" ("id") on delete cascade deferrable initially deferred
);)");
			}
			else if (version == 10)
			{
				ScanSettings::get(session).modify()->addAudioFileExtension(".m4b");
				ScanSettings::get(session).modify()->addAudioFileExtension(".alac");
			}
			else if (version == 11)
			{
				// Sanitize bad MBID, need to rescan the whole files
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 12)
			{
				// Artist and release that have a badly parsed name but a MBID had no chance to updat the name
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 13)
			{
				// Always store UUID in lower case + better WMA parsing
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 14)
			{
				// SortName now set from metadata
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 15)
			{
				session.getDboSession().execute("ALTER TABLE user ADD ui_theme INTEGER NOT NULL DEFAULT(" + std::to_string(static_cast<int>(User::defaultUITheme)) + ")");
			}
			else if (version == 16)
			{
				session.getDboSession().execute("ALTER TABLE track ADD total_disc INTEGER NOT NULL DEFAULT(0)");
				session.getDboSession().execute("ALTER TABLE track ADD total_track INTEGER NOT NULL DEFAULT(0)");

				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 17)
			{
				// Drop colums total_disc/total_track from release
				session.getDboSession().execute(R"(
CREATE TABLE "release_backup" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "name" text not null,
  "mbid" text not null
))");
				session.getDboSession().execute("INSERT INTO release_backup SELECT id,version,name,mbid FROM release");
				session.getDboSession().execute("DROP TABLE release");
				session.getDboSession().execute("ALTER TABLE release_backup RENAME TO release");

				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 18)
			{
				session.getDboSession().execute(R"(
CREATE TABLE IF NOT EXISTS "subsonic_settings" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "api_enabled" boolean not null,
  "artist_list_mode" integer not null
))");
			}
			else if (version == 19)
			{
				session.getDboSession().execute(R"(
CREATE TABLE "user_backup" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "type" integer not null,
  "login_name" text not null,
  "password_salt" text not null,
  "password_hash" text not null,
  "last_login" text,
  "subsonic_transcode_enable" boolean not null,
  "subsonic_transcode_format" integer not null,
  "subsonic_transcode_bitrate" integer not null,
  "subsonic_artist_list_mode" integer not null,
  "ui_theme" integer not null,
  "cur_playing_track_pos" integer not null,
  "repeat_all" boolean not null,
  "radio" boolean not null
))");
				session.getDboSession().execute(std::string {"INSERT INTO user_backup SELECT id, version, type, login_name, password_salt, password_hash, last_login, "}
						+ (User::defaultSubsonicTranscodeEnable ? "1" : "0")
						+ ", " + std::to_string(static_cast<int>(User::defaultSubsonicTranscodeFormat))
						+ ", " + std::to_string(User::defaultSubsonicTranscodeBitrate)
						+ ", " + std::to_string(static_cast<int>(User::defaultSubsonicArtistListMode))
						+ ", ui_theme, cur_playing_track_pos, repeat_all, radio FROM user");
				session.getDboSession().execute("DROP TABLE user");
				session.getDboSession().execute("ALTER TABLE user_backup RENAME TO user");
			}
			else if (version == 20)
			{
				session.getDboSession().execute("DROP TABLE subsonic_settings");
			}
			else if (version == 21)
			{
				session.getDboSession().execute("ALTER TABLE track ADD track_replay_gain REAL");
				session.getDboSession().execute("ALTER TABLE track ADD release_replay_gain REAL");

				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 22)
			{
				session.getDboSession().execute("ALTER TABLE track ADD disc_subtitle TEXT NOT NULL DEFAULT ''");

				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 23)
			{
				// Better cover detection
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 24)
			{
				// User's AuthMode
				session.getDboSession().execute("ALTER TABLE user ADD auth_mode INTEGER NOT NULL DEFAULT(" + std::to_string(static_cast<int>(/*User::defaultAuthMode*/0)) + ")");
			}
			else if (version == 25)
			{
				// Better cover detection
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 26)
			{
				// Composer, mixer, etc. support
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 27)
			{
				// Composer, mixer, etc. support, now fallback on MBID tagged entries as there is no mean to provide MBID by tags for these kinf od artists
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 28)
			{
				// Drop Auth mode
				session.getDboSession().execute(R"(
CREATE TABLE "user_backup" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "type" integer not null,
  "login_name" text not null,
  "password_salt" text not null,
  "password_hash" text not null,
  "last_login" text,
  "subsonic_transcode_enable" boolean not null,
  "subsonic_transcode_format" integer not null,
  "subsonic_transcode_bitrate" integer not null,
  "subsonic_artist_list_mode" integer not null,
  "ui_theme" integer not null,
  "cur_playing_track_pos" integer not null,
  "repeat_all" boolean not null,
  "radio" boolean not null
))");
				session.getDboSession().execute("INSERT INTO user_backup SELECT id, version, type, login_name, password_salt, password_hash, last_login, subsonic_transcode_enable, subsonic_transcode_format, subsonic_transcode_bitrate, subsonic_artist_list_mode, ui_theme, cur_playing_track_pos, repeat_all, radio FROM user");
				session.getDboSession().execute("DROP TABLE user");
				session.getDboSession().execute("ALTER TABLE user_backup RENAME TO user");
			}
			else if (version == 29)
			{
				session.getDboSession().execute("ALTER TABLE tracklist_entry ADD date_time TEXT");
				session.getDboSession().execute("ALTER TABLE user ADD listenbrainz_token TEXT");
				session.getDboSession().execute("ALTER TABLE user ADD scrobbler INTEGER NOT NULL DEFAULT(" + std::to_string(static_cast<int>(User::defaultScrobbler)) + ")");
				session.getDboSession().execute("ALTER TABLE track ADD recording_mbid TEXT");

				session.getDboSession().execute("DELETE from tracklist WHERE name = ?").bind("__played_tracks__");

				// MBID changes
				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 30)
			{
				// drop "year" and "original_year" (rescan needed to convert them into dates)
				session.getDboSession().execute(R"(
CREATE TABLE "track_backup" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "scan_version" integer not null,
  "track_number" integer not null,
  "disc_number" integer not null,
  "name" text not null,
  "duration" integer,
  "date" integer text,
  "original_date" integer text,
  "file_path" text not null,
  "file_last_write" text,
  "file_added" text,
  "has_cover" boolean not null,
  "mbid" text not null,
  "copyright" text not null,
  "copyright_url" text not null,
  "release_id" bigint, total_disc INTEGER NOT NULL DEFAULT(0), total_track INTEGER NOT NULL DEFAULT(0), track_replay_gain REAL, release_replay_gain REAL, disc_subtitle TEXT NOT NULL DEFAULT '', recording_mbid TEXT,
  constraint "fk_track_release" foreign key ("release_id") references "release" ("id") on delete cascade deferrable initially deferred
))");
				session.getDboSession().execute("INSERT INTO track_backup SELECT id, version, scan_version, track_number, disc_number, name, duration, \"1900-01-01\", \"1900-01-01\", file_path, file_last_write, file_added, has_cover, mbid, copyright, copyright_url, release_id, total_disc, total_track, track_replay_gain, release_replay_gain, disc_subtitle, recording_mbid FROM track");
				session.getDboSession().execute("DROP TABLE track");
				session.getDboSession().execute("ALTER TABLE track_backup RENAME TO track");

				// Just increment the scan version of the settings to make the next scheduled scan rescan everything
				ScanSettings::get(session).modify()->incScanVersion();
			}
			else if (version == 31)
			{
				// new star system, using dedicated ObjectSets per scrobbler
				session.getDboSession().execute("DROP TABLE user_artist_starred");
				session.getDboSession().execute("DROP TABLE user_release_starred");
				session.getDboSession().execute("DROP TABLE user_track_starred");

				session.getDboSession().execute(R"(
CREATE TABLE IF NOT EXISTS "starred_artist" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "scrobbler" integer not null,
  "date_time" text,
  "artist_id" bigint,
  "user_id" bigint,
  constraint "fk_starred_artist_artist" foreign key ("artist_id") references "artist" ("id") on delete cascade deferrable initially deferred,
  constraint "fk_starred_artist_user" foreign key ("user_id") references "user" ("id") on delete cascade deferrable initially deferred
))");

				session.getDboSession().execute(R"(
CREATE TABLE IF NOT EXISTS "starred_release" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "scrobbler" integer not null,
  "date_time" text,
  "release_id" bigint,
  "user_id" bigint,
  constraint "fk_starred_release_release" foreign key ("release_id") references "release" ("id") on delete cascade deferrable initially deferred,
  constraint "fk_starred_release_user" foreign key ("user_id") references "user" ("id") on delete cascade deferrable initially deferred
))");

				session.getDboSession().execute(R"(
CREATE TABLE IF NOT EXISTS "starred_track" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "scrobbler" integer not null,
  "date_time" text,
  "track_id" bigint,
  "user_id" bigint,
  constraint "fk_starred_track_track" foreign key ("track_id") references "track" ("id") on delete cascade deferrable initially deferred,
  constraint "fk_starred_track_user" foreign key ("user_id") references "user" ("id") on delete cascade deferrable initially deferred
))");

				// new listen system, no longer using tracklists
				session.getDboSession().execute(R"(
CREATE TABLE IF NOT EXISTS "listen" (
  "id" integer primary key autoincrement,
  "version" integer not null,
  "date_time" text,
  "scrobbler" integer not null,
  "scrobbling_state" integer not null,
  "track_id" bigint,
  "user_id" bigint,
  constraint "fk_listen_track" foreign key ("track_id") references "track" ("id") on delete cascade deferrable initially deferred,
  constraint "fk_listen_user" foreign key ("user_id") references "user" ("id") on delete cascade deferrable initially deferred
))");
			}
			else
			{
				LMS_LOG(DB, ERROR) << "Database version " << version << " cannot be handled using migration";
				throw LmsException { LMS_DATABASE_VERSION > version  ? outdatedMsg : "Server binary outdated, please upgrade it to handle this database"};
			}

			VersionInfo::get(session).modify()->setVersion(++version);
		}
	}
}