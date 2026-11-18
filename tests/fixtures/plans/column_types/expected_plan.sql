CREATE TABLE "public"."everything" (
    "a" int2,
    "b" int4,
    "c" int8,
    "d" numeric(12,4),
    "e" numeric,
    "f" float4,
    "g" float8,
    "h" text,
    "i" varchar(40),
    "j" bpchar(3),
    "k" bool,
    "l" date,
    "m" timestamp,
    "n" timestamptz,
    "o" timestamptz(3),
    "p" uuid,
    "q" jsonb DEFAULT '{}',
    "r" text[] DEFAULT '{}',
    "s" int4[]
);

