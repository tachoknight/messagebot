/*
 * messagebot.cpp Written by Ron Olson (tachoknight@gmail.com) on 7/1/2017
 *
 * This is an IRC bot that uses the awesome libircclient library
 * (http://www.ulduzsoft.com/libircclient/) and SQLite
 * (https://www.sqlite.org) to provide a way to send both public and private
 * messages to users when they log in, or switch nicks.
 *
 * Compile with the flags:
 *  -std=c++11 -ldl -Wall -m64 -g -O2 -O3 -DENABLE_IPV6
 *  -DENABLE_THREADS -D_REENTRANT -DENABLE_SSL -pthread
 * Linker flags:
 *  -L<wherever libircclient libs are installed> -lircclient -lpthread -lssl
 *  -lcrypto -lnsl
 */
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <tuple>
#include <iterator>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
using namespace std;

#include <libircclient.h>
#include <libirc_rfcnumeric.h>

#include "sqlite3.h"

/* The sqlite database */
sqlite3 *db;

// IRC session context
typedef struct {
    char *channel;
    char *nick;
} irc_ctx_t;


/****************************************************************************
 * U T I L I T Y  S T U F F
 ***************************************************************************/

std::string return_current_time_and_date()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

#ifndef MODERN_GCC
    char gccTimeCompromise[24] = {0};
    strftime(gccTimeCompromise, sizeof(gccTimeCompromise), "%m/%d/%Y %X", std::localtime(&in_time_t));
    return gccTimeCompromise;
#else
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%m/%d/%Y %X");
    return ss.str();
#endif
}

void split(const string& str, vector<string>& v) {
    std::stringstream ss(str);
    ss >> std::noskipws;
    std::string field;
    char ws_delim;
    while(1) {
        if( ss >> field )
            v.push_back(field);
        else if (ss.eof())
            break;
        else
            v.push_back(std::string());
        ss.clear();
        ss >> ws_delim;
    }
}

// trim from start
static inline std::string &ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                    std::not1(std::ptr_fun<int, int>(std::isspace))));
    return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
    return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
    return ltrim(rtrim(s));
}


/****************************************************************************
 * M E S S A G E
 ***************************************************************************/

struct message {
    int id = 0;
    string message;
    string fromUser;
    string toUser;
    string dateAdded;
    int isPrivate = 0;

    message() {
        dateAdded = return_current_time_and_date();
    }

    string buildMessage() const {
        stringstream msg;
        msg << "[B]Hi " << toUser << "! " << fromUser << " on " << dateAdded << " wanted to tell you";
        if (isPrivate) {
            msg << " (privately)";
        }

        // Header in bold, message underlined
        msg << " this:[/B] [U]" << message;
        msg << "[/U]";

        return msg.str();
    }
};


/****************************************************************************
 * D A T A B A S E  S T U F F
 ***************************************************************************/


vector<message> getMessages(const string& toUser) {
    vector<message> messages;

    string sql =  "select id, message, from_user, date_added, is_private from messages where to_user = '";
    sql += toUser;
    sql += "'";

    sqlite3_stmt *res;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &res, 0);
    if (rc != SQLITE_OK) {
        cout << "Failed to fetch data: " << sqlite3_errmsg(db) << "\n";
        return messages;
    }

    // Now go through all the records we got back...
    while ((rc = sqlite3_step(res)) == SQLITE_ROW) {
        message r;
        r.toUser = toUser;
        r.id =  sqlite3_column_int(res, 0);
        r.message = string(reinterpret_cast<const char*>(sqlite3_column_text(res, 1)));
        r.fromUser = string(reinterpret_cast<const char*>(sqlite3_column_text(res, 2)));
        r.dateAdded = string(reinterpret_cast<const char*>(sqlite3_column_text(res, 3)));
        if (sqlite3_column_type(res, 4) != SQLITE_NULL)
            r.isPrivate = sqlite3_column_int(res, 4);

        messages.push_back(r);
    }

    sqlite3_finalize(res);

    return messages;
}

void removeOldMessages(const vector<message> messages) {
    for_each(messages.begin(), messages.end(), [&](const message& r) {
        string delSQL = "delete from messages where id = ";
        delSQL += to_string(r.id);

        sqlite3_stmt* delStmt = NULL;
        int rc = sqlite3_prepare_v2(db, delSQL.c_str(), -1, &delStmt, NULL);
        if (rc != SQLITE_OK) {
            cerr << "prepare failed: " << sqlite3_errmsg(db) << endl;
            return;
        }

        rc = sqlite3_step(delStmt);
        if (rc != SQLITE_DONE) {
            cerr << "step failed: " << sqlite3_errmsg(db) << endl;
            return;
        }

        sqlite3_finalize(delStmt);
    });
}

bool addNewMessage(message& r) {
    int rc = 0;

    sqlite3_stmt* insertStmt = NULL;
    rc = sqlite3_prepare_v2(db, "insert into messages (id, message, from_user, to_user, date_added, is_private) values (NULL, ?, ?, ?, ?, ?)", -1, &insertStmt, NULL);
    if (rc != SQLITE_OK) {
        cerr << "prepare failed: " << sqlite3_errmsg(db) << endl;
        return false;
    }

    sqlite3_bind_text(insertStmt, 1, r.message.c_str(), (int)r.message.length(), 0);
    sqlite3_bind_text(insertStmt, 2, r.fromUser.c_str(), (int)r.fromUser.length(), 0);
    sqlite3_bind_text(insertStmt, 3, r.toUser.c_str(), (int)r.toUser.length(), 0);
    sqlite3_bind_text(insertStmt, 4, r.dateAdded.c_str(), (int)r.dateAdded.length(), 0);
    sqlite3_bind_int(insertStmt, 5, r.isPrivate);

    rc = sqlite3_step(insertStmt);
    if (rc != SQLITE_DONE) {
        cerr << "step failed: " << sqlite3_errmsg(db) << endl;
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(insertStmt);

    return true;
}

/****************************************************************************
 * C R E A T E  A N D  S A V E  T H E  M E S S A G E
 ***************************************************************************/

tuple<bool, string> saveMessage(string fromUser, string unparsedText, bool isPrivate) {
    bool isOkay = true;
    string retMsg;

    // Split our string into tokens (we know there will be at least one because
    // that included the command that got us here)
    vector<string> tokens;
    split(unparsedText, tokens);

    if (tokens.size() < 3) {
        isOkay = false;
        retMsg = "!msg <user> <message>";
    } else {
        message r;
        r.fromUser = fromUser;
        r.toUser = tokens[1];

        string msg;
        for (unsigned int pos = 2; pos < tokens.size(); ++pos) {
            msg += tokens[pos];
            msg += " ";
        }

        r.message = trim(msg);

        if (isPrivate) {
            r.isPrivate = 1;
        } else {
            r.isPrivate = 0;
        }

        // And now save it to the database
        isOkay = addNewMessage(r);
        if (isOkay == false) {
            retMsg = "Hmm, I couldn't store the message in the database. Might just want to tell them yourself.";
        }
    }

    return make_tuple(isOkay, retMsg);
}

/****************************************************************************
 * I R C  S T U F F
 ***************************************************************************/

void sendMessage(irc_session_t* session, string toUser) {
    cout << "Gonna check to see if there's anything for " << toUser << endl;
    // Check to see if there's any messages
    auto messages = getMessages(toUser);
    if (messages.size()) {
        // Ah we have one or more messages for this person
        for_each(messages.begin(), messages.end(), [&](const message& r) {
            cout << "Okay, we're gonna send a " << (r.isPrivate == 1 ? "private" : "broadcast") << " message to " << r.toUser << "\n";

            // Add some color
            char* fancyText = irc_color_convert_to_mirc (r.buildMessage().c_str());
            if (r.isPrivate) {
                irc_cmd_msg (session, toUser.c_str(), fancyText);
            } else {
                irc_ctx_t *ctx = (irc_ctx_t *) irc_get_ctx(session);
                irc_cmd_msg (session, ctx->channel, fancyText);
            }

            // And make sure to free the allocated fancy string...
            free (fancyText);
        });

        // We've sent the messages, so we can delete them from the database so we don't
        // send them again
        removeOldMessages(messages);
    }
}

void event_join(irc_session_t * session, const char *event,
                const char *origin, const char **params,
                unsigned int count)
{
    char nickbuf[128];
    irc_target_get_nick (origin, nickbuf, sizeof(nickbuf));

    sendMessage(session, nickbuf);
}

void event_connect(irc_session_t * session, const char *event,
                   const char *origin, const char **params,
                   unsigned int count) {
    irc_ctx_t *ctx = (irc_ctx_t *) irc_get_ctx(session);
    irc_cmd_join(session, ctx->channel, 0);
}

void event_channel(irc_session_t * session, const char *event,
                   const char *origin, const char **params,
                   unsigned int count) {
    if ( !origin || count != 2 )
        return;

    char nickbuf[128];
    irc_target_get_nick (origin, nickbuf, sizeof(nickbuf));
    irc_ctx_t *ctx = (irc_ctx_t *) irc_get_ctx(session);

    // Format is
    //  !msg <toUser> <message>

    cout << "TEXT: " << params[1] << "\n";
    if (strncmp (params[1], "!msg", 4) == 0) {
        auto ok = saveMessage(nickbuf, params[1], false);
        string reply;
        if (std::get<0>(ok)) {
            reply = "Okay, ";
            reply += nickbuf;
            reply += ", whenever they log on (or switch nicks) I'll let them know";
        } else {
            reply = std::get<1>(ok);
        }

        irc_cmd_msg (session, ctx->channel, reply.c_str());
    }
}

void event_privmsg (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count) {
    if (!origin || count != 2)
        return;

    char nickbuf[128];
    irc_target_get_nick (origin, nickbuf, sizeof(nickbuf));

    cout << "PRIVATE TEXT: " << params[1] << "\n";
    if (strncmp (params[1], "!msg", 4) == 0) {
        auto ok = saveMessage(nickbuf, params[1], true);
        string reply;
        if (std::get<0>(ok)) {
            reply = "Okay, ";
            reply += nickbuf;
            reply += ", whenever they log on (or switch nicks) I'll let them know";
        } else {
            reply = std::get<1>(ok);
        }

        irc_cmd_msg (session, nickbuf, reply.c_str());
    }
}

void event_nick (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count) {
    if ( !origin || count != 1 )
        return;

    // params[0] contains the new nick, which is what we want to check for messages
    sendMessage(session, params[0]);
}

void event_numeric(irc_session_t * session, unsigned int event,
                   const char *origin, const char **params,
                   unsigned int count) {

    // On a join event, we are sent to this function as a callback, and
    // the message we get is LIBIRC_RFC_RPL_NAMREPLY; this contains the
    // list of people in this room, so we can use this to go through and
    // see who here needs a message delivered
    if (event == LIBIRC_RFC_RPL_NAMREPLY) {
        std::string fulltext;
        for (unsigned int i = 0; i < count; i++ ) {
            if (i > 0)
                fulltext += " ";

            fulltext += params[i];
        }

        vector<string> users;
        split(fulltext, users);

        // For each of the users in the room...
        for_each(users.begin(), users.end(), [&](const string& user) {
            sendMessage(session, user);
        });
    }
}

/*
 * If the bot is going to be used in multiple rooms, make sure that each room has
 * its own database
 */
int main(int argc, char *argv[]) {
    if (argc != 5) {
        cout << "Usage:" << argv[0] << " <server> <nick> <channel> <database>\n";
        return 1;
    }

    // Now start setting up the IRC stuff...
    auto port = 6667;
    irc_callbacks_t callbacks;
    irc_ctx_t ctx;
    irc_session_t *s;

    // Open the database
    int rc = sqlite3_open(argv[4], &db);
    if( rc ) {
        cerr << "Can't open database: " << sqlite3_errmsg(db) << "\n";
        goto exit;
    }

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.event_connect = event_connect;
    callbacks.event_join = event_join;
    callbacks.event_channel = event_channel;
    callbacks.event_privmsg = event_privmsg;
    callbacks.event_numeric = event_numeric;
    callbacks.event_nick = event_nick;

    s = irc_create_session(&callbacks);
    if (!s) {
        cerr << "Drat, couldn't create the session so we didn't start\n" <<
             endl;
        goto exit;
    }

    ctx.nick = argv[2];
    ctx.channel = argv[3];

    irc_set_ctx(s, &ctx);

    if (strchr(argv[1], ':') != 0)
        port = 0;

    if (argv[1][0] == '#' && argv[1][1] == '#') {
        argv[1]++;
        irc_option_set(s, LIBIRC_OPTION_SSL_NO_VERIFY);
    }

    if (irc_connect(s, argv[1], port, 0, argv[2], 0, 0)) {
        cerr << "Could not connect: " << irc_strerror(irc_errno(s)) << "\n";
        goto exit;
    }

    // And enter the loop that will run forever, generating events...
    if (irc_run(s)) {
        cerr << "Could not connect or I/O error: " << irc_strerror(irc_errno(s)) << endl;
        goto exit;
    }

exit:
    // And if we're here then we've somehow exited the loop above or had some kind of error...
    sqlite3_close(db);

    return 1;
}
