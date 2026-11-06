<?php
// Run with PHP CLI. Creates one temporary account/character and removes it in finally.
declare(strict_types=1);
require __DIR__ . '/service.php';
$db = registrationDatabase(registrationConfig());
$suffix = substr(strtr(bin2hex(random_bytes(8)), '0123456789', 'abcdefghij'), 0, 12);
$email = 'codex-registration-' . $suffix . '@example.invalid';
$name = 'Codex ' . ucfirst($suffix);
$password = 'Temporary9' . bin2hex(random_bytes(10));
function callRegistration(array $data): array {
    $context = stream_context_create(['http' => ['method' => 'POST', 'timeout' => 10,
        'header' => "Content-Type: application/json\r\n", 'content' => json_encode($data)]]);
    return json_decode(file_get_contents('http://127.0.0.1/otc-registration.php', false, $context), true, 16, JSON_THROW_ON_ERROR);
}
function checkRegistration(bool $ok, string $label): void {
    if (!$ok) throw new RuntimeException($label);
    echo 'PASS: ', $label, PHP_EOL;
}
try {
    checkRegistration(isset(callRegistration(['type' => 'getaccountcreationstatus'])['Worlds'][0]['Name']), 'world status');
    checkRegistration(callRegistration(['type' => 'checkemail', 'Email' => 'invalid'])['IsValid'] === false, 'invalid email rejected');
    checkRegistration(callRegistration(['type' => 'checkpassword', 'Password1' => 'weak'])['PasswordValid'] === false, 'weak password rejected');
    $data = ['type' => 'createaccountandcharacter', 'EMail' => $email, 'Password' => $password,
        'CharacterName' => $name, 'CharacterSex' => 'male', 'AcceptedTerms' => true];
    checkRegistration(callRegistration(array_merge($data, ['AcceptedTerms' => false]))['Success'] === false, 'terms required');
    checkRegistration(callRegistration($data)['Success'] === true, 'account and character created through HTTP');
    $q = $db->prepare('SELECT a.password,a.type,p.group_id,p.sex,p.level,p.posx,p.posy,p.posz FROM accounts a JOIN players p ON p.account_id=a.id WHERE a.email=?');
    $q->execute([$email]); $row = $q->fetch();
    checkRegistration($row && hash_equals(sha1($password), $row['password']), 'password compatible with TFS authentication');
    checkRegistration((int)$row['type'] === 1 && (int)$row['group_id'] === 1 && (int)$row['sex'] === 1 && (int)$row['level'] === 8 && $row['posx'] > 0 && $row['posy'] > 0, 'normal player permissions and valid spawn');
    checkRegistration(callRegistration($data)['Success'] === false, 'duplicate registration rejected');
    $character = ['type' => 'createcharacter', 'Account' => $email, 'Password' => $password,
        'CharacterName' => 'Second ' . ucfirst($suffix), 'CharacterSex' => 'female',
        'World' => registrationConfig()['world'], 'PlayTutorial' => false];
    checkRegistration(callRegistration(array_merge($character, ['Password' => 'WrongPassword9']))['Success'] === false, 'character creation rejects wrong credentials');
    checkRegistration(callRegistration(array_merge($character, ['World' => 'InvalidWorld']))['Success'] === false, 'character creation rejects wrong world');
    checkRegistration(callRegistration($character)['Success'] === true, 'character created on existing account');
    $q = $db->prepare('SELECT COUNT(*) FROM players p JOIN accounts a ON a.id=p.account_id WHERE a.email=?');
    $q->execute([$email]);
    checkRegistration((int)$q->fetchColumn() === 2, 'both characters belong to same account');
    checkRegistration(callRegistration($character)['Success'] === false, 'duplicate character rejected');
    checkRegistration(callRegistration(['type' => 'checkemail', 'Email' => $email])['IsValid'] === false, 'registered email unavailable');
    checkRegistration(callRegistration(['type' => 'checkcharactername', 'CharacterName' => $name])['IsAvailable'] === false, 'registered character unavailable');
} finally {
    $q = $db->prepare('DELETE FROM accounts WHERE email = ? AND name LIKE ?');
    $q->execute([$email, 'otc%']);
    echo 'Temporary test records removed.', PHP_EOL;
}
