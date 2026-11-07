<?php
declare(strict_types=1);
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');
function registrationReply(array $value, int $status = 200): void {
    http_response_code($status); echo json_encode($value); exit;
}
// This installation is local. Do not expose database registration over public HTTP.
if (!in_array($_SERVER['REMOTE_ADDR'] ?? '', ['127.0.0.1', '::1'], true) || isset($_SERVER['HTTP_ORIGIN'])) {
    registrationReply(['Success' => false, 'errorMessage' => 'Local client access only.'], 403);
}
if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') registrationReply(['Success' => false, 'errorMessage' => 'POST required.'], 405);
if ((int)($_SERVER['CONTENT_LENGTH'] ?? 0) > 8192) registrationReply(['Success' => false, 'errorMessage' => 'Request too large.'], 413);
try {
    $data = json_decode(file_get_contents('php://input', false, null, 0, 8193), true, 16, JSON_THROW_ON_ERROR);
    if (!is_array($data)) throw new InvalidArgumentException('Expected JSON object.');
    foreach ($data as $value) if (!is_scalar($value) && $value !== null) throw new InvalidArgumentException('Invalid field.');
    require_once __DIR__ . '/service.php';
    $config = registrationConfig();
    registrationReply(registrationHandle($data, registrationDatabase($config), $config));
} catch (JsonException | InvalidArgumentException $e) {
    registrationReply(['Success' => false, 'errorMessage' => 'Invalid registration request.'], 400);
} catch (Throwable $e) {
    error_log('OTC registration: ' . $e->getMessage());
    registrationReply(['Success' => false, 'errorMessage' => 'Registration is unavailable. Please try again later.']);
}
