#!/usr/bin/python3

"""
Script for creating a v1 index.db used to test the `uenv image upgrade` functionality.
Run with uenv v4 or v5 - not guaranteed to continue working indefinately.
We keep a copy of a v1 index.db in the repo for running the tests, so hopefully
you never have to read or run this.
"""

import pathlib
import sys
import sqlite3

prefix = pathlib.Path(__file__).parent.resolve()
lib =  prefix.parent / "lib"
sys.path = [lib.as_posix()] + sys.path
print(sys.path)

import record

prgenvgnu_records = [
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "default", "monday", 1024, "a"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "v2",     "monday", 1024, "a"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "v1",     "monday", 1024, "c"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "24.2",  "default", "monday", 1024, "b"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "24.2",  "v1",     "monday", 1024, "b"*64),
]


create_db_command = """
BEGIN;

PRAGMA foreign_keys=on;

CREATE TABLE images (
    sha256 TEXT PRIMARY KEY CHECK(length(sha256)==64),
    id TEXT UNIQUE CHECK(length(id)==16),
    date TEXT NOT NULL,
    size INTEGER NOT NULL,
    uarch TEXT NOT NULL,
    system TEXT NOT NULL
);

CREATE TABLE uenv (
    version_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    version TEXT NOT NULL,
    UNIQUE (name, version)
);

CREATE TABLE tags (
    version_id INTEGER,
    tag TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    PRIMARY KEY (version_id, tag),
    FOREIGN KEY (version_id)
        REFERENCES uenv (version_id)
            ON DELETE CASCADE
            ON UPDATE CASCADE,
    FOREIGN KEY (sha256)
        REFERENCES images (sha256)
            ON DELETE CASCADE
            ON UPDATE CASCADE
);

-- for convenient generation of the Record type used internally by uenv-image
CREATE VIEW records AS
SELECT
    images.system  AS system,
    images.uarch   AS uarch,
    uenv.name      AS name,
    uenv.version   AS version,
    tags.tag       AS tag,
    images.date    AS date,
    images.size    AS size,
    tags.sha256    AS sha256,
    images.id      AS id
FROM tags
    INNER JOIN uenv   ON uenv.version_id = tags.version_id
    INNER JOIN images ON images.sha256   = tags.sha256;

COMMIT;
"""

def create_db(path):
    store = sqlite3.connect(path)
    store.executescript(create_db_command)

    return store

def add_record(store, r):
    cursor = store.cursor()

    cursor.execute("BEGIN;")
    cursor.execute("PRAGMA foreign_keys=on;")
    cursor.execute("INSERT OR IGNORE INTO images (sha256, id, date, size, uarch, system) VALUES (?, ?, ?, ?, ?, ?)",
                   (r.sha256, r.id, r.date, r.size, r.uarch, r.system))
    # Insert a new name/version to the uenv table if no existing images with that pair exist
    cursor.execute("INSERT OR IGNORE INTO uenv (name, version) VALUES (?, ?)",
                   (r.name, r.version))
    # Retrieve the version_id of the name/version pair
    # This requires a SELECT query to get the correct version_id whether or not
    # a new row was added in the last INSERT
    cursor.execute("SELECT version_id FROM uenv WHERE name = ? AND version = ?",
                   (r.name, r.version))
    version_id = cursor.fetchone()[0]
    # Check whether an image with the same tag already exists in the repos
    cursor.execute("SELECT version_id, tag, sha256 FROM tags WHERE version_id = ? AND tag = ?",
                   (version_id, r.tag))
    existing_tag = cursor.fetchone()
    # If the tag exists, update the entry in the table with the new sha256
    if existing_tag:
        cursor.execute("UPDATE tags SET sha256 = ? WHERE version_id = ? AND tag = ?",
                       (r.sha256, version_id, r.tag))
    # Else add a new row
    else:
        cursor.execute("INSERT INTO tags (version_id, tag, sha256) VALUES (?, ?, ?)",
                       (version_id, r.tag, r.sha256))

    # Commit the transaction
    store.commit()


if __name__ == "__main__":
    store = create_db("index1.db")
    for r in prgenvgnu_records:
        add_record(store, r)
    store.close()

