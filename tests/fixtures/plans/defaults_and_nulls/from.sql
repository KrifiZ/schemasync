CREATE TABLE settings (
    id int PRIMARY KEY,
    theme text DEFAULT 'light',
    retries int NOT NULL,
    updated_at timestamptz DEFAULT CURRENT_TIMESTAMP,
    label varchar(20) DEFAULT 'x'
);
