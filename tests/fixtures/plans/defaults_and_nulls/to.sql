CREATE TABLE settings (
    id int PRIMARY KEY,
    theme text,
    retries int DEFAULT 3,
    updated_at timestamptz DEFAULT now(),
    label varchar(20) DEFAULT 'x'::character varying
);
