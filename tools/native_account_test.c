/* Offline checks for the native half of the player accounts: the PBKDF2-SHA256
 * stretch must match the browser byte for byte, nicks must follow the same
 * rules as the website, and every record the game writes or reads is built and
 * parsed here against known answers. No network, no threads. */
#include <stdio.h>
#include <string.h>
#include "online_account.h"

static int failures;
static void check(int ok, const char *what) {
    if (!ok) { printf("FAIL: %s\n", what); failures++; }
}

int main(void) {
    char salt[ON_HASH_SIZE], hash[ON_HASH_SIZE], buffer[4096];

    /* The salt is sha256("pvg3-account:" + nick), exactly like the website. */
    struct { const char *login, *password, *salt, *hash; } cases[] = {
        {"qwertyuiopaj1234", "my-password",
         "8c8e1686e5b893af8053c9e23776a74815da6f4cfa3097229e43f7f6c75b3e09",
         "20b961f93441bcba1937cfe33284dda2b8c2f07745a99f1b5efa571566662438"},
        {"player_1", "1234567",
         "2b26af9d108863e55396d230f6533bd868ae436a66950afec6b4840d2bf3c7df",
         "a0fa7410fc1816cbab914a3679cc7298540702d3a29f91854a5ba95f04a99245"},
        {"A", "123456",
         "de8e3b12f70f0bff76b97c745e25071457d37e47023da6c36ba75b1872f18fdb",
         "71bc494c8acb951b3ef7a6b479d8cd7088cfc02c50d3c2d44dfae7b9a0ca751c"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        on_account_salt(cases[i].login, salt);
        check(!strcmp(salt, cases[i].salt), "salt matches the web client");
        on_account_hash(cases[i].login, cases[i].password, hash);
        check(!strcmp(hash, cases[i].hash), "PBKDF2 hash matches the web client");
    }

    /* Nicks: no spaces, no punctuation, no duplicates by case or spaces. */
    char name[ON_LOGIN_SIZE];
    check(on_account_valid_login("qwertyuiopaj1234"), "a plain nick is valid");
    check(on_account_valid_login(" QwErTyUiOpAj1234 "), "spaces and case are folded");
    check(!on_account_valid_login("bad nick"), "a space inside is rejected");
    const char *punctuation = "@&+?!:;₽()¢°√™[§£€";
    for (const char *bad = punctuation; *bad; ) {
        char probe[8];
        int length = 1;
        while (((unsigned char)bad[length] >> 6) == 2) length++;
        snprintf(probe, sizeof probe, "ab%.*s", length, bad);
        check(!on_account_valid_login(probe), "punctuation is rejected");
        bad += length;
    }
    check(!on_account_valid_login("ab"), "two characters are too short");
    char long_name[40];
    memset(long_name, 'a', sizeof long_name);
    long_name[24] = 0;
    check(on_account_valid_login(long_name), "24 characters are allowed");
    long_name[24] = 'a';long_name[25] = 0;
    check(!on_account_valid_login(long_name), "25 characters are too long");
    check(on_account_normalize(name, "  MiXeD_9 \n"), "normalize trims");
    check(!strcmp(name, "mixed_9"), "normalize lower-cases");
    char token[ON_TOKEN_SIZE];memset(token, 'a', 64);token[64] = 0;
    check(on_account_valid_token(token), "a 64-character lowercase hex session token is valid");
    token[12] = 'Z';
    check(!on_account_valid_token(token), "uppercase or non-hex session token is rejected");

    check(!on_account_valid_password("short"), "a five-character password is refused");
    check(!on_account_valid_password("a b c d e"), "spaces in a password are refused");
    check(on_account_valid_password("1234567"), "seven characters are enough");

    /* Builders: measured size equals the written size and the JSON is valid. */
    size_t need = on_account_record_json(salt, hash, 1700000000000LL, NULL, 0);
    check(need > 0 && need < sizeof buffer, "the account record is measured");
    check(on_account_record_json(salt, hash, 1700000000000LL, buffer, need) == 0,
          "a buffer without room for NUL is refused");
    check(on_account_record_json(salt, hash, 1700000000000LL, buffer, need + 1) == need,
          "the account record is written");
    check(strstr(buffer, "\"salt\":\"") && strstr(buffer, "\"createdAt\":1700000000000"),
          "the account record carries salt and time");

    need = on_account_token_json(hash, hash, NULL, 0);
    check(on_account_token_json(hash, hash, buffer, need + 1) == need, "token json");
    need = on_account_author_json("qwertyuiopaj1234", hash, NULL, 0);
    check(on_account_author_json("qwertyuiopaj1234", hash, buffer, need + 1) == need,
          "author json");
    check(!strstr(buffer, "\\\\"), "the author record needs no escapes");
    need = on_account_comment_json("qwertyuiopaj1234", "привет \"мир\"\n", hash, 42, NULL, 0);
    check(on_account_comment_json("qwertyuiopaj1234", "привет \"мир\"\n", hash, 42,
                                  buffer, need + 1) == need, "comment json");
    check(!!strstr(buffer, "\\\"мир\\\""), "quotes are escaped");
    check(!!strstr(buffer, "\\n"), "a newline is escaped");
    OnComment sample = {{0}, {0}, {0}, 7, 0};
    snprintf(sample.id, sizeof sample.id, "%s", "0123456789abcdef");
    snprintf(sample.login, sizeof sample.login, "%s", "someone");
    snprintf(sample.text, sizeof sample.text, "%s", "плохое сообщение");
    need = on_account_comment_hide_json(&sample, "qwertyuiopaj1234", hash, NULL, 0);
    check(on_account_comment_hide_json(&sample, "qwertyuiopaj1234", hash, buffer,
                                       need + 1) == need, "hide json");
    check(strstr(buffer, "\"hidden\":true") && strstr(buffer, "\"by\":\"qwertyuiopaj1234\""),
          "hiding marks the moderator");
    need = on_account_ban_json(1, "непроходимый уровень", "qwertyuiopaj1234", hash, 5, NULL, 0);
    check(on_account_ban_json(1, "непроходимый уровень", "qwertyuiopaj1234", hash, 5,
                              buffer, need + 1) == need, "ban json");
    check(!!strstr(buffer, "\"banned\":true"), "a ban is recorded");
    check(on_account_flag_json(1, buffer, sizeof buffer) == 4 && !strcmp(buffer, "true"),
          "the official flag is a bare boolean");
    check(on_account_flag_json(0, NULL, 0) == 5, "the flag measures itself");

    /* Parsers. */
    check(on_account_parse_admin("true") == 1, "an admin record is true");
    check(on_account_parse_admin(" null\n") == 0, "a missing admin record is false");
    check(on_account_parse_admin("false") == 0, "a removed admin record is false");
    check(on_account_parse_admin("<html>") == 0, "garbage is not an admin record");

    OnBan bans[ON_BANS_CAP];
    int count = on_account_parse_bans(
        "{\"rude\":{\"banned\":true,\"reason\":\"плохое сообщение\",\"at\":12,"
        "\"by\":\"qwertyuiopaj1234\",\"tok\":\"x\"},"
        "\"nice\":{\"banned\":false,\"reason\":\"исправился\",\"at\":13,\"by\":\"q\"},"
        "\"broken\":{\"banned\":true}}", bans, ON_BANS_CAP);
    check(count == 2, "only active bans are kept");
    check(count == 2 && !strcmp(bans[0].login, "rude"), "the ban keeps its nick");
    check(count == 2 && !strcmp(bans[0].reason, "плохое сообщение"), "the ban keeps its reason");
    check(count == 2 && bans[0].at == 12, "the ban keeps its time");
    check(on_account_is_banned(bans, count, "rude") == 1, "the banned nick is recognised");
    check(on_account_is_banned(bans, count, " RUDE ") == 1, "the ban is case-insensitive");
    check(on_account_is_banned(bans, count, "nice") == 0, "an unbanned nick is free");
    check(on_account_parse_bans("null", bans, ON_BANS_CAP) == 0, "no bans at all");
    check(on_account_parse_bans("{oops", bans, ON_BANS_CAP) == 0, "broken bans are empty");

    OnComment comments[ON_COMMENTS_CAP];
    count = on_account_parse_comments(
        "{\"bbbbbbbb\":{\"login\":\"second\",\"text\":\"второй\",\"at\":20},"
        "\"aaaaaaaa\":{\"login\":\"first\",\"text\":\"первый\",\"at\":10},"
        "\"zz\":{\"login\":\"broken\",\"text\":\"x\",\"at\":1},"
        "\"cccccccc\":{\"login\":\"hidden\",\"text\":\"спрятан\",\"at\":30,\"hidden\":true}}",
        comments, ON_COMMENTS_CAP);
    check(count == 3, "well-formed comments are read");
    check(count == 3 && !strcmp(comments[0].login, "first"), "comments are oldest first");
    check(count == 3 && !strcmp(comments[2].text, "спрятан"), "the newest comment is last");
    check(count == 3 && comments[2].hidden == 1, "a hidden comment is marked");
    check(on_account_parse_comments("null", comments, ON_COMMENTS_CAP) == -1,
          "a missing branch is reported as broken");
    check(on_account_parse_comments("{}", comments, ON_COMMENTS_CAP) == 0,
          "an empty branch has no comments");
    check(on_account_parse_comments("[{", comments, ON_COMMENTS_CAP) == -1,
          "an array is not a comment branch");

    if (!failures) printf("Native accounts: crypto, nicks, builders and parsers passed\n");
    return failures ? 1 : 0;
}
