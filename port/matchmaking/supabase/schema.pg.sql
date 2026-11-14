-- The matchmaking tables for Postgres (Supabase): schema.sql's, with the row number SQLite keeps by itself (rowid:
-- a quick match takes the room that waited longest) as a column. In a schema of their own, bt3: the function
-- reaches them through the database's own connection, and nothing is exposed through the project's API.
CREATE SCHEMA IF NOT EXISTS bt3;
CREATE TABLE IF NOT EXISTS bt3.rooms (rowid BIGSERIAL, code TEXT PRIMARY KEY, made BIGINT NOT NULL, version BIGINT NOT NULL, ip TEXT NOT NULL, host_key TEXT NOT NULL, join_key TEXT, host_name TEXT NOT NULL, join_name TEXT, host_addrs TEXT NOT NULL, join_addrs TEXT, queue TEXT, who TEXT, lobby BIGINT, loc TEXT, join_region TEXT);
CREATE TABLE IF NOT EXISTS bt3.used (what TEXT NOT NULL, slot BIGINT NOT NULL, n BIGINT NOT NULL, PRIMARY KEY (what, slot));
CREATE TABLE IF NOT EXISTS bt3.watchers (key TEXT PRIMARY KEY, code TEXT NOT NULL, made BIGINT NOT NULL, name TEXT NOT NULL, addrs TEXT NOT NULL);
