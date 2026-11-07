# OTC-Fonticak registration API

The local XAMPP installation serves `http://127.0.0.1/otc-registration.php`.
Its entry point at `C:/xampp/htdocs/otc-registration.php` loads
`tools/registration/endpoint.php` from this repository. The client config is
`C:/Users/fabim/Downloads/OTC-Fonticak/init.lua`, `Services.createAccount`.
Restart the client after changing the URL. Apache and MySQL must be running.
No game server rebuild is necessary.

The service reads database credentials and new player settings from `config.lua`.
It does not execute Lua. Values must be literal strings or numbers.
When new player X/Y are zero, it uses the Account Manager's saved position.
Accounts receive a generated internal name, the supplied email, normal permissions,
and a SHA-1 password hash matching this TFS engine. Players start with vocation 0
and the configured level, health, mana, capacity and town. Use the email to log in.

Supported JSON POST types match the existing Fonticak form:
`getaccountcreationstatus`, `generatecharactername`, `checkemail`,
`checkcharactername`, `checkpassword`, `createaccountandcharacter`.
Creation requires `EMail`, `Password`, `CharacterName`, `CharacterSex`
(`male`/`female`) and boolean `AcceptedTerms: true`.
Success returns `{"Success":true}`; errors include `errorMessage`.
The `createcharacter` request authenticates `Account` and `Password`, then creates
`CharacterName`/`CharacterSex` in that account. It requires the configured `World`
and `PlayTutorial: false`. Accounts with two-factor authentication are currently
rejected by this endpoint. The client reloads the character list after creation.
Creation validates all fields again and inserts account/player in a transaction.
A database advisory lock serializes registrations from this endpoint.

This endpoint accepts loopback requests only and rejects browser Origin headers.
It is configured for this computer, not a public registration website. For remote
players, deploy behind HTTPS with a dedicated restricted database user and rate
limiting, and adapt the explicit endpoint access rule. Other account creation
systems must coordinate email uniqueness too; the TFS schema has no unique email key.
Never place `config.lua` or `service.php` in the public web directory.

Run `C:/xampp/php/php.exe tools/registration/smoke.php` to exercise the HTTP API.
It creates a temporary account/character, verifies the database values and duplicate
rejection, and deletes its temporary records in a finally block.
