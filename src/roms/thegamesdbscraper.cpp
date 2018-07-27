/***
 * Copyright (c) 2013, Dan Hasting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the organization nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***/

#include "thegamesdbscraper.h"

#include "../global.h"
#include "../common.h"

#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>


TheGamesDBScraper::TheGamesDBScraper(QWidget *parent, bool force) : QObject(parent)
{
    this->parent = parent;
    this->force = force;
    this->keepGoing = true;
}


QString TheGamesDBScraper::convertIDs(QJsonObject foundGame, QString typeName, QString listName)
{
    QJsonArray idArray = foundGame.value(typeName).toArray();

    QString cacheFileString = getDataLocation() + "/cache/" + typeName + ".json";
    QFile cacheFile(cacheFileString);

    cacheFile.open(QIODevice::ReadOnly);
    QString data = cacheFile.readAll();
    cacheFile.close();

    QJsonDocument document = QJsonDocument::fromJson(data.toUtf8());
    QJsonObject json = document.object();
    QJsonObject cache = json.value("data").toObject().value(typeName).toObject();

    QString result = "";

    foreach (QJsonValue id, idArray)
    {
        QString entryID = QString::number(id.toInt());
        QString entryName = cache.value(entryID).toObject().value("name").toString();

        if (entryName == "") {
            updateListCache(&cacheFile, listName);
            cacheFile.open(QIODevice::ReadOnly);
            data = cacheFile.readAll();
            cacheFile.close();

            document = QJsonDocument::fromJson(data.toUtf8());
            json = document.object();
            cache = json.value("data").toObject().value(typeName).toObject();
            entryName = cache.value(entryID).toObject().value("name").toString();
        }

        if (entryName != "")
            result += entryName + ", ";
    }

    int pos = result.lastIndexOf(QChar(','));
    return result.left(pos);
}


void TheGamesDBScraper::deleteGameInfo(QString fileName, QString identifier)
{
    QString text;
    text = QString(tr("<b>NOTE:</b> If you are deleting this game's information because the game doesn't "))
                 + tr("exist on TheGamesDB and <AppName> pulled the information for different game, it's ")
                 + tr("better to create an account on")+" <a href=\"http://thegamesdb.net/\">TheGamesDB</a> "
                 + tr("and add the game so other users can benefit as well.")
                 + "<br /><br />"
                 + tr("This will cause <AppName> to not update the information for this game until you ")
                 + tr("force it with \"Download/Update Info...\"")
                 + "<br /><br />"
                 + tr("Delete the current information for") + " <b>" + fileName + "</b>?";
    text.replace("<AppName>",AppName);

    int answer = QMessageBox::question(parent, tr("Delete Game Information"), text,
                                       QMessageBox::Yes | QMessageBox::No);

    if (answer == QMessageBox::Yes) {
        QString gameCache = getDataLocation() + "/cache/" + identifier.toLower();

        QString dataFile = gameCache + "/data.xml";
        QFile file(dataFile);

        // Remove game information
        file.open(QIODevice::WriteOnly);
        QTextStream stream(&file);
        stream << "NULL";
        file.close();

        // Remove cover image
        QString coverFile = gameCache + "/boxart-front.";

        QFile coverJPG(coverFile + "jpg");
        QFile coverPNG(coverFile + "png");

        if (coverJPG.exists())
            coverJPG.remove();
        if (coverPNG.exists())
            coverPNG.remove();

        coverJPG.open(QIODevice::WriteOnly);
        QTextStream streamImage(&coverJPG);
        streamImage << "";
        coverJPG.close();
    }
}


void TheGamesDBScraper::downloadGameInfo(QString identifier, QString searchName, QString gameID)
{
    if (keepGoing && identifier != "") {
        if (force) parent->setEnabled(false);

        bool updated = false;

        QString gameCache = getDataLocation() + "/cache/" + identifier.toLower();
        QDir cache(gameCache);

        if (!cache.exists()) {
            cache.mkpath(gameCache);
        }

        QString genreCache = getDataLocation() + "/cache/genres.json";
        QFile genres(genreCache);
        if (!genres.exists())
            updateListCache(&genres, "Genres");

        QString developerCache = getDataLocation() + "/cache/developers.json";
        QFile developers(developerCache);
        if (!developers.exists())
            updateListCache(&developers, "Developers");

        QString publisherCache = getDataLocation() + "/cache/publishers.json";
        QFile publishers(publisherCache);
        if (!publishers.exists())
            updateListCache(&publishers, "Publishers");

        //Get game JSON info from thegamesdb.net
        QString dataFile = gameCache + "/data.json";
        QFile file(dataFile);

        if (!file.exists() || file.size() == 0 || force) {
            QUrl url;

            //Remove [!], (U), etc. from GoodName for searching
            searchName.remove(QRegExp("\\W*(\\(|\\[).+(\\)|\\])\\W*"));

            //Few game specific hacks
            //TODO: Contact thegamesdb.net and see if these can be fixed on their end
            if (searchName == "Legend of Zelda, The - Majora's Mask")
                searchName = "Majora's Mask";
            else if (searchName == "Legend of Zelda, The - Ocarina of Time" ||
                     searchName == "THE LEGEND OF ZELDA")
                searchName = "The Legend of Zelda: Ocarina of Time";
            else if (searchName == "Tsumi to Batsu - Hoshi no Keishousha")
                searchName = "Sin and Punishment";
            else if (searchName == "1080 Snowboarding")
                searchName = "1080: TenEighty Snowboarding";
            else if (searchName == "Extreme-G XG2")
                searchName = "Extreme-G 2";
            else if (searchName.contains("Pokemon"))
                searchName.replace("Pokemon", "Pokémon");
            else if (searchName.toLower() == "f-zero x")
                gameID = "10836";

            QString apiFilter = "&filter[platform]=3&include=boxart&fields=game_title,release_date,";
            apiFilter += "developers,publishers,genres,overview,rating,players";

            //If user submits gameID, use that
            if (gameID != "")
                url.setUrl("https://api.thegamesdb.net/Games/ByGameID?apikey=" + TheGamesDBAPIKey + "&id="
                           + gameID + apiFilter);
            else
                url.setUrl("https://api.thegamesdb.net/Games/ByGameName?apikey=" + TheGamesDBAPIKey + "&name="
                           + searchName + apiFilter);

            QString data = getUrlContents(url);

            QJsonDocument document = QJsonDocument::fromJson(data.toUtf8());
            QJsonObject json = document.object();

            if (json.value("code").toInt() != 200 && json.value("code").toInt() != 0) {
                QString status = json.value("status").toString();
                QString message;
                message = QString(tr("The following error from TheGamesDB occured while downloading:"))
                                   + "<br /><br />" + status;
                QMessageBox::information(parent, QObject::tr("Download Error"), message);
                if (force) parent->setEnabled(true);
                return;
            }

            QJsonValue games = json.value("data").toObject().value("games");
            QJsonArray gamesArray = games.toArray();


            int count = 0, found = 0;

            foreach (QJsonValue game, gamesArray)
            {
                QJsonValue title = game.toObject().value("game_title");

                if (force) { //from user dialog
                    QJsonValue date = game.toObject().value("release_date");

                    QString check = "Game: " + title.toString();
                    check.remove(QRegExp(QString("[^A-Za-z 0-9 \\.,\\?'""!@#\\$%\\^&\\*\\")
                                         + "(\\)-_=\\+;:<>\\/\\\\|\\}\\{\\[\\]`~]*"));
                    if (date.toString() != "") check += "\n" + tr("Released on: ") + date.toString();
                    check += "\n\n" + tr("Does this look correct?");

                    int answer = QMessageBox::question(parent, QObject::tr("Game Information Download"),
                                                       check, QMessageBox::Yes | QMessageBox::No);

                    if (answer == QMessageBox::Yes) {
                        found = count;
                        updated = true;
                        break;
                    }
                } else {
                    //We only want one game, so search for a perfect match in the GameTitle element.
                    //Otherwise this will default to 0 (the first game found)
                    if(title.toString() == searchName)
                        found = count;
                }

                count++;
            }

            if (!force || updated) {
                QJsonObject foundGame = gamesArray.at(found).toObject();
                QJsonObject saveData;

                QString gameID = QString::number(foundGame.value("id").toInt());
                QJsonObject boxart = json.value("include").toObject().value("boxart").toObject();

                QString thumbURL = boxart.value("base_url").toObject().value("thumb").toString();
                QJsonArray imgArray = boxart.value("data").toObject().value(gameID).toArray();

                QString frontImg = "";

                foreach (QJsonValue img, imgArray)
                {
                    QString type = img.toObject().value("type").toString();
                    QString side = img.toObject().value("side").toString();
                    QString filename = img.toObject().value("filename").toString();

                    if (type == "boxart" && side == "front")
                        frontImg = thumbURL + filename;
                }

                //Convert IDs from API to text names
                QString genresString = convertIDs(foundGame, "genres", "Genres");
                QString developerString = convertIDs(foundGame, "developers", "Developers");
                QString publisherString = convertIDs(foundGame, "publishers", "Publishers");


                saveData.insert("game_title", foundGame.value("game_title"));
                saveData.insert("release_date", foundGame.value("release_date"));
                saveData.insert("rating", foundGame.value("rating"));
                saveData.insert("overview", foundGame.value("overview"));
                saveData.insert("players", foundGame.value("players"));
                saveData.insert("boxart", frontImg);
                saveData.insert("genres", genresString);
                saveData.insert("developer", developerString);
                saveData.insert("publisher", publisherString);

                QJsonDocument document(saveData);

                file.open(QIODevice::WriteOnly);
                file.write(document.toJson());
                file.close();
            }

            if (force && !updated) {
                QString message;

                if (count == 0)
                    message = QObject::tr("No results found.");
                else
                    message = QObject::tr("No more results found.");

                QMessageBox::information(parent, QObject::tr("Game Information Download"), message);
            }
        }


        //Get front cover
        QString boxartURL = "";
        QString boxartExt = "";
        QString coverFile = gameCache + "/boxart-front.";

        QFile coverJPG(coverFile + "jpg");
        QFile coverPNG(coverFile + "png");

        if ((!coverJPG.exists() && !coverPNG.exists()) || (force && updated)) {
            file.open(QIODevice::ReadOnly);
            QString data = file.readAll();
            file.close();

            QJsonDocument document = QJsonDocument::fromJson(data.toUtf8());
            QJsonObject json = document.object();
            QString boxartURL = json.value("boxart").toString();

            if (boxartURL != "") {
                QUrl url(boxartURL);

                //Delete current box art
                QFile::remove(coverFile + "jpg");
                QFile::remove(coverFile + "png");

                //Check to save as JPG or PNG
                boxartExt = QFileInfo(boxartURL).completeSuffix().toLower();
                QFile cover(coverFile + boxartExt);

                cover.open(QIODevice::WriteOnly);
                cover.write(getUrlContents(url));
                cover.close();
            }
        }

        if (updated)
            QMessageBox::information(parent, QObject::tr("Game Information Download"),
                                     QObject::tr("Download Complete!"));

        if (force) parent->setEnabled(true);
    }
}


QByteArray TheGamesDBScraper::getUrlContents(QUrl url)
{
    QNetworkAccessManager *manager = new QNetworkAccessManager;

    QNetworkRequest request;
    request.setUrl(url);
    request.setRawHeader("User-Agent", AppName.toUtf8().constData());
    QNetworkReply *reply = manager->get(request);

    QTimer timer;
    timer.setSingleShot(true);

    QEventLoop loop;
    connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));

    int time = SETTINGS.value("Other/networktimeout", 10).toInt();
    if (time == 0) time = 10;
    time *= 1000;

    timer.start(time);
    loop.exec();

    if(timer.isActive()) { //Got reply
        timer.stop();

        if(reply->error() > 0)
            showError(reply->errorString());
        else
            return reply->readAll();

    } else //Request timed out
        showError(tr("Request timed out. Check your network settings."));

    return QByteArray();
}


void TheGamesDBScraper::showError(QString error)
{
    QString question = "\n\n" + tr("Continue scraping information?");

    if (force)
        QMessageBox::information(parent, tr("Network Error"), error);
    else {
        int answer = QMessageBox::question(parent, tr("Network Error"), error + question,
                                           QMessageBox::Yes | QMessageBox::No);

        if (answer == QMessageBox::No)
            keepGoing = false;
    }
}


void TheGamesDBScraper::updateListCache(QFile *file, QString list)
{
    QUrl url;
    url.setUrl("https://api.thegamesdb.net/" + list + "?apikey=" + TheGamesDBAPIKey);
    QString data = getUrlContents(url);
    QJsonDocument document = QJsonDocument::fromJson(data.toUtf8());

    file->open(QIODevice::WriteOnly);
    file->write(document.toJson());
    file->close();
}
