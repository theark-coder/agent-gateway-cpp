-- Execute as a MySQL administrator against the agent_gateway database.
-- CREATE DATABASE agent_gateway CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE agent_gateway;

CREATE TABLE IF NOT EXISTS tasks (
    id                  INT NOT NULL AUTO_INCREMENT PRIMARY KEY,
    actor_id            VARCHAR(64) NOT NULL,
    title               VARCHAR(255) NOT NULL,
    status              VARCHAR(24) NOT NULL DEFAULT 'pending',
    idempotency_key     VARCHAR(128) NULL,
    request_fingerprint VARCHAR(512) NOT NULL,
    version             INT UNSIGNED NOT NULL DEFAULT 0,
    created_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at          TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
                        ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uq_actor_idempotency (actor_id, idempotency_key),
    KEY idx_actor_created (actor_id, created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
