CREATE TABLE users (
    id int PRIMARY KEY,
    email text,
    name varchar(100),
    active boolean NOT NULL DEFAULT true,
    score numeric(10,2) DEFAULT 0,
    token uuid DEFAULT gen_random_uuid(),
    ticket bigserial
);
