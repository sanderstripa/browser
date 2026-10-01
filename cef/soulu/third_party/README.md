SQLite 3.53.4 (public domain), official amalgamation:
https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
Archive SHA3-256: 628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e

Only sqlite3.c and sqlite3.h are included. The importer opens local encrypted
snapshots, never the original browser database. Loadable extensions are disabled.
