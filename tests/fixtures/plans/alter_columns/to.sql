CREATE TABLE items (
    id int PRIMARY KEY,
    qty bigint,
    label varchar(100),
    price numeric(12,2),
    note text NOT NULL DEFAULT ''
);
