CREATE TABLE users (
    id serial PRIMARY KEY,
    email varchar(255) NOT NULL UNIQUE,
    display_name text,
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE posts (
    id bigserial PRIMARY KEY,
    author_id int NOT NULL REFERENCES users (id) ON DELETE CASCADE,
    title text NOT NULL,
    body text,
    published boolean NOT NULL DEFAULT false
);

CREATE INDEX ON posts (author_id);

CREATE TABLE customer_loyalty_program_memberships_archive (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    previous_membership_holder_user_id int REFERENCES users (id)
);
