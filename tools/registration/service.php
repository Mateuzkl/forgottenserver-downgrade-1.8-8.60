<?php
declare(strict_types=1);

function registrationConfig(): array {
    $lua = file_get_contents(dirname(__DIR__, 2) . '/config.lua');
    $get = static function (string $key, $default) use ($lua) {
        if (preg_match('/^' . preg_quote($key, '/') . '\s*=\s*"([^"\r\n]*)"/m', $lua, $m)) return $m[1];
        if (preg_match('/^' . preg_quote($key, '/') . '\s*=\s*(\d+)/m', $lua, $m)) return (int)$m[1];
        return $default;
    };
    return [
        'host' => $get('mysqlHost', '127.0.0.1'), 'port' => $get('mysqlPort', 3306),
        'database' => $get('mysqlDatabase', ''), 'user' => $get('mysqlUser', ''), 'password' => $get('mysqlPass', ''),
        'world' => $get('serverName', 'Forgotten'), 'worldType' => $get('worldType', 'pvp'), 'level' => $get('newPlayerLevel', 8),
        'magic' => $get('newPlayerMagicLevel', 0), 'health' => $get('newPlayerHealth', 185),
        'mana' => $get('newPlayerMana', 90), 'cap' => $get('newPlayerCap', 470),
        'town' => $get('newPlayerTownId', 1), 'x' => $get('newPlayerSpawnPosX', 0),
        'y' => $get('newPlayerSpawnPosY', 0), 'z' => $get('newPlayerSpawnPosZ', 0)
    ];
}

function registrationDatabase(array $c): PDO {
    return new PDO("mysql:host={$c['host']};port={$c['port']};dbname={$c['database']};charset=utf8", $c['user'], $c['password'],
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_EMULATE_PREPARES => false, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
}

function registrationPassword(string $password): array {
    $requirements = [
        'HasUpperCase' => (bool)preg_match('/[A-Z]/', $password), 'HasLowerCase' => (bool)preg_match('/[a-z]/', $password),
        'HasNumber' => (bool)preg_match('/[0-9]/', $password), 'PasswordLength' => strlen($password) >= 8 && strlen($password) <= 64,
        'InvalidCharacters' => (bool)preg_match('/^[\x21-\x7e]+$/D', $password)
    ];
    $valid = !in_array(false, $requirements, true);
    return ['PasswordValid' => $valid, 'PasswordRequirements' => $requirements,
        'PasswordStrength' => $valid ? 100 : 0, 'PasswordStrengthColor' => $valid ? '#76EE00' : '#EC644B'];
}

function registrationExists(PDO $db, string $sql, string $value): bool {
    $q = $db->prepare($sql); $q->execute([$value]); return (bool)$q->fetchColumn();
}

function registrationHandle(array $data, PDO $db, array $c): array {
    $type = $data['type'] ?? '';
    $email = trim((string)($data['EMail'] ?? $data['Email'] ?? ''));
    $name = trim((string)($data['CharacterName'] ?? ''));
    $password = (string)($data['Password'] ?? $data['Password1'] ?? '');
    $emailValid = strlen($email) <= 254 && filter_var($email, FILTER_VALIDATE_EMAIL);
    $nameValid = (bool)preg_match('/^[A-Z][a-z]+(?:[ -][A-Z]?[a-z]+)*$/D', $name) && strlen($name) >= 3 && strlen($name) <= 29
        && !preg_match('/^(account manager|god|gm|cm|admin)( |$)/i', $name);
    $emailSql = 'SELECT id FROM accounts WHERE LOWER(email) = LOWER(?) LIMIT 1';
    $nameSql = 'SELECT id FROM players WHERE LOWER(name) = LOWER(?) LIMIT 1';
    switch ($type) {
        case 'getaccountcreationstatus':
            return ['RecommendedWorld' => $c['world'], 'IsCaptchaDeactivated' => true, 'Worlds' => [[
                'Name' => $c['world'], 'Region' => 'Local', 'PvPType' => ['pvp' => 'Open PvP', 'no-pvp' => 'Optional PvP', 'pvp-enforced' => 'Hardcore PvP'][$c['worldType']] ?? 'Open PvP',
                'PlayersOnline' => (int)$db->query('SELECT COUNT(*) FROM players_online')->fetchColumn(),
                'PremiumOnly' => 0, 'TransferType' => 'Blocked', 'CreationDate' => 0,
                'BattlEyeActivationTimestamp' => 0, 'BattlEyeInitiallyActive' => 0
            ]]];
        case 'generatecharactername':
            $first = ['Ar', 'Bel', 'Dar', 'El', 'Mar', 'Nor', 'Ral', 'Val'];
            $last = ['adan', 'dor', 'ion', 'omar', 'oth', 'rian', 'van', 'wyn'];
            for ($i = 0; $i < 30; $i++) {
                $generated = $first[random_int(0, 7)] . $last[random_int(0, 7)];
                if (!registrationExists($db, $nameSql, $generated)) return ['GeneratedName' => $generated];
            }
            return ['GeneratedName' => 'Adventurer ' . $first[random_int(0, 7)] . $last[random_int(0, 7)]];
        case 'checkemail':
            $valid = $emailValid && !registrationExists($db, $emailSql, $email)
                && !registrationExists($db, 'SELECT id FROM accounts WHERE LOWER(name) = LOWER(?) LIMIT 1', $email);
            return ['IsValid' => (bool)$valid, 'errorCode' => $valid ? null : 59,
                'errorMessage' => $valid ? '' : 'Invalid email address or email already registered.'];
        case 'checkcharactername':
            $valid = $nameValid && !registrationExists($db, $nameSql, $name);
            return ['IsAvailable' => $valid, 'errorMessage' => $valid ? '' : 'Invalid character name or name already taken.'];
        case 'checkpassword': return registrationPassword($password);
        case 'createcharacter':
        case 'createaccountandcharacter':
            $existing = $type === 'createcharacter';
            $accountId = null;
            if ($existing) {
                $login = (string)($data['Account'] ?? '');
                $q = $db->prepare('SELECT id,password,secret FROM accounts WHERE LOWER(name)=LOWER(?) OR (email <> ? AND LOWER(email)=LOWER(?))');
                $q->execute([$login, '', $login]);
                $accounts = $q->fetchAll();
                if (count($accounts) !== 1 || !hash_equals($accounts[0]['password'], sha1($password)) || !empty($accounts[0]['secret'])) {
                    return ['Success' => false, 'errorMessage' => 'Account authentication failed or account requires two-factor authentication.'];
                }
                $accountId = (int)$accounts[0]['id'];
                if (($data['World'] ?? '') !== $c['world'] || ($data['PlayTutorial'] ?? false) !== false) {
                    return ['Success' => false, 'errorMessage' => 'Invalid world or tutorial not available on this server.'];
                }
            }
            if ((!$existing && (!$emailValid || !registrationPassword($password)['PasswordValid'])) || !$nameValid
                || !in_array($data['CharacterSex'] ?? '', ['male', 'female'], true)
                || (!$existing && ($data['AcceptedTerms'] ?? false) !== true)) {
                return ['Success' => false, 'errorMessage' => 'Check your email, name, password, sex and agreement acceptance.'];
            }
            // Serialize this endpoint's registrations so concurrent requests cannot reuse an email.
            if (!$db->query("SELECT GET_LOCK('otc_account_registration', 5)")->fetchColumn()) {
                return ['Success' => false, 'errorMessage' => 'Registration is busy. Please try again.'];
            }
            try {
                $db->beginTransaction();
                if (registrationExists($db, $nameSql, $name) || (!$existing && (registrationExists($db, $emailSql, $email)
                    || registrationExists($db, 'SELECT id FROM accounts WHERE LOWER(name) = LOWER(?) LIMIT 1', $email)))) {
                    $db->rollBack(); return ['Success' => false, 'errorMessage' => 'Email or character name already registered.'];
                }
                if ($existing) {
                    $q = $db->prepare('SELECT COUNT(*) FROM players WHERE account_id=? AND deletion=0');
                    $q->execute([$accountId]);
                    if ((int)$q->fetchColumn() >= 20) {
                        $db->rollBack(); return ['Success' => false, 'errorMessage' => 'This account already has 20 characters.'];
                    }
                }
                $x = $c['x']; $y = $c['y']; $z = $c['z'];
                if (!$x || !$y) {
                    $spawn = $db->query("SELECT posx,posy,posz FROM players WHERE name = 'Account Manager' LIMIT 1")->fetch();
                    if (!$spawn || !$spawn['posx'] || !$spawn['posy']) throw new RuntimeException('Configure new player spawn coordinates.');
                    $x = $spawn['posx']; $y = $spawn['posy']; $z = $spawn['posz'];
                }
                if (!$existing) {
                    $q = $db->prepare('INSERT INTO accounts (name,password,email,creation,type) VALUES (?,?,?,?,1)');
                    $q->execute(['otc' . bin2hex(random_bytes(10)), sha1($password), $email, time()]);
                    $accountId = (int)$db->lastInsertId();
                }
                $level = max(1, (int)$c['level']);
                $experience = (int)((50 * $level ** 3 - 300 * $level ** 2 + 850 * $level - 600) / 3);
                $sex = $data['CharacterSex'] === 'male' ? 1 : 0;
                $q = $db->prepare('INSERT INTO players (name,account_id,group_id,level,vocation,health,healthmax,experience,maglevel,mana,manamax,cap,town_id,posx,posy,posz,sex,looktype) VALUES (?,?,1,?,0,?,?,?,?,?,?,?,?,?,?,?,?,?)');
                $q->execute([$name, $accountId, $level, $c['health'], $c['health'], $experience, $c['magic'], $c['mana'], $c['mana'], $c['cap'], $c['town'], $x, $y, $z, $sex, $sex ? 128 : 136]);
                $db->commit(); return ['Success' => true];
            } catch (Throwable $e) {
                if ($db->inTransaction()) $db->rollBack();
                throw $e;
            } finally { $db->query("SELECT RELEASE_LOCK('otc_account_registration')"); }
        default: return ['Success' => false, 'errorMessage' => 'Unknown registration request.'];
    }
}
