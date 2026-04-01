#include "CricNodeFixtureBrowser.hpp"

#include <QDate>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

CricNodeFixtureBrowser::CricNodeFixtureBrowser(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Browse Fixtures");
	setMinimumSize(700, 500);

	auto *layout = new QVBoxLayout(this);

	/* Provider selection */
	auto *providerRow = new QHBoxLayout();
	providerRow->addWidget(new QLabel("Provider:"));
	providerCombo = new QComboBox();
	providerCombo->addItem("CricClubs", "cricclubs");
	providerCombo->addItem("DCL (Dallas Cricket League)", "dcl");
	providerCombo->addItem("Play-Cricket", "playcricket");
	providerCombo->addItem("PlayHQ", "playhq");
	providerRow->addWidget(providerCombo);
	layout->addLayout(providerRow);

	connect(providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&CricNodeFixtureBrowser::OnProviderChanged);

	/* ID input (club ID, site ID, grade ID, etc.) */
	auto *idRow = new QHBoxLayout();
	idLabel = new QLabel("Club ID:");
	idRow->addWidget(idLabel);
	idInput = new QLineEdit();
	idInput->setPlaceholderText("Enter ID...");
	idRow->addWidget(idInput);
	layout->addLayout(idRow);

	/* Token input (for Play-Cricket and PlayHQ) */
	auto *tokenRow = new QHBoxLayout();
	tokenLabel = new QLabel("API Token:");
	tokenRow->addWidget(tokenLabel);
	tokenInput = new QLineEdit();
	tokenInput->setPlaceholderText("Enter API token...");
	tokenRow->addWidget(tokenInput);
	layout->addLayout(tokenRow);
	tokenLabel->hide();
	tokenInput->hide();

	/* Fetch button */
	fetchButton = new QPushButton("Fetch Fixtures");
	connect(fetchButton, &QPushButton::clicked, this, &CricNodeFixtureBrowser::OnFetchClicked);
	layout->addWidget(fetchButton);

	/* Progress */
	progressBar = new QProgressBar();
	progressBar->setVisible(false);
	layout->addWidget(progressBar);

	/* Status */
	statusLabel = new QLabel("");
	layout->addWidget(statusLabel);

	/* Fixture table */
	fixtureTable = new QTableWidget();
	fixtureTable->setColumnCount(6);
	fixtureTable->setHorizontalHeaderLabels({"Team 1", "Team 2", "Date", "Time", "Venue", "Status"});
	fixtureTable->horizontalHeader()->setStretchLastSection(true);
	fixtureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	fixtureTable->setSelectionMode(QAbstractItemView::SingleSelection);
	fixtureTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	layout->addWidget(fixtureTable);

	/* Bottom buttons */
	auto *btnRow = new QHBoxLayout();
	selectButton = new QPushButton("Select Match");
	selectButton->setEnabled(false);
	connect(selectButton, &QPushButton::clicked, this, &CricNodeFixtureBrowser::OnSelectClicked);
	cancelButton = new QPushButton("Cancel");
	connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
	btnRow->addStretch();
	btnRow->addWidget(selectButton);
	btnRow->addWidget(cancelButton);
	layout->addLayout(btnRow);

	connect(fixtureTable, &QTableWidget::itemSelectionChanged, this, [this]() {
		selectButton->setEnabled(fixtureTable->currentRow() >= 0);
	});

	networkManager = new QNetworkAccessManager(this);
	connect(networkManager, &QNetworkAccessManager::finished, this,
		&CricNodeFixtureBrowser::OnNetworkReply);

	OnProviderChanged(0);
}

CricNodeFixtureBrowser::~CricNodeFixtureBrowser() {}

void CricNodeFixtureBrowser::OnProviderChanged(int index)
{
	QString provider = providerCombo->itemData(index).toString();
	currentProvider = provider.toStdString();

	if (provider == "cricclubs") {
		idLabel->setText("Club ID:");
		idInput->setPlaceholderText("e.g. 123");
		tokenLabel->hide();
		tokenInput->hide();
	} else if (provider == "dcl") {
		idLabel->setText("(No ID needed)");
		idInput->setPlaceholderText("Leave empty — fetches all DCL matches");
		tokenLabel->hide();
		tokenInput->hide();
	} else if (provider == "playcricket") {
		idLabel->setText("Site ID:");
		idInput->setPlaceholderText("e.g. 12345");
		tokenLabel->show();
		tokenInput->show();
		tokenLabel->setText("API Token:");
		tokenInput->setPlaceholderText("Play-Cricket API token");
	} else if (provider == "playhq") {
		idLabel->setText("Grade ID (UUID):");
		idInput->setPlaceholderText("e.g. abc123-...");
		tokenLabel->show();
		tokenInput->show();
		tokenLabel->setText("API Key:");
		tokenInput->setPlaceholderText("PlayHQ API key");
	}
}

void CricNodeFixtureBrowser::OnFetchClicked()
{
	fixtures.clear();
	fixtureTable->setRowCount(0);
	selectButton->setEnabled(false);

	if (currentProvider == "dcl") {
		FetchDclFixtures();
	} else if (currentProvider == "playcricket") {
		FetchPlayCricketFixtures();
	} else if (currentProvider == "playhq") {
		FetchPlayHQFixtures();
	} else if (currentProvider == "cricclubs") {
		FetchCricClubsFixtures();
	}
}

void CricNodeFixtureBrowser::FetchDclFixtures()
{
	SetStatus("Fetching DCL fixtures...");
	progressBar->setVisible(true);
	progressBar->setRange(0, 0); /* indeterminate */
	dclPagesLoaded = 0;
	dclTotalPages = 10;

	/* Fetch first page */
	QUrl url("https://dallascricket.org:3000/api/getmatchlists");
	QUrlQuery query;
	query.addQueryItem("offset", "0");
	query.addQueryItem("query", "");
	query.addQueryItem("filter", "1");
	query.addQueryItem("isSearchById", "false");
	url.setQuery(query);

	QNetworkRequest request(url);
	request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
	request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
	request.setTransferTimeout(30000);
	networkManager->get(request);
}

void CricNodeFixtureBrowser::FetchPlayCricketFixtures()
{
	QString siteId = idInput->text().trimmed();
	QString token = tokenInput->text().trimmed();
	if (siteId.isEmpty() || token.isEmpty()) {
		QMessageBox::warning(this, "Missing Info", "Please enter Site ID and API Token.");
		return;
	}

	SetStatus("Fetching Play-Cricket fixtures...");
	progressBar->setVisible(true);
	progressBar->setRange(0, 0);

	QUrl url("https://play-cricket.com/api/v2/result_summary.json");
	QUrlQuery query;
	query.addQueryItem("site_id", siteId);
	query.addQueryItem("season", QString::number(QDate::currentDate().year()));
	query.addQueryItem("api_token", token);
	url.setQuery(query);

	QNetworkRequest request(url);
	request.setTransferTimeout(30000);
	networkManager->get(request);
}

void CricNodeFixtureBrowser::FetchPlayHQFixtures()
{
	QString gradeId = idInput->text().trimmed();
	QString apiKey = tokenInput->text().trimmed();
	if (gradeId.isEmpty() || apiKey.isEmpty()) {
		QMessageBox::warning(this, "Missing Info", "Please enter Grade ID and API Key.");
		return;
	}

	SetStatus("Fetching PlayHQ fixtures...");
	progressBar->setVisible(true);
	progressBar->setRange(0, 0);

	QUrl url(QString("https://api.playhq.com/v2/grades/%1/games").arg(gradeId));
	QNetworkRequest request(url);
	request.setRawHeader("x-api-key", apiKey.toUtf8());
	request.setRawHeader("x-phq-tenant", "ca");
	request.setTransferTimeout(30000);
	networkManager->get(request);
}

void CricNodeFixtureBrowser::FetchCricClubsFixtures()
{
	QString clubId = idInput->text().trimmed();
	if (clubId.isEmpty()) {
		QMessageBox::warning(this, "Missing Info", "Please enter a CricClubs Club ID.");
		return;
	}

	cricClubsClubId = clubId.toStdString();

	SetStatus("Fetching CricClubs fixtures...");
	progressBar->setVisible(true);
	progressBar->setRange(0, 0);

	int currentYear = QDate::currentDate().year();
	QUrl url(QString("https://cricclubs.com/club/fixtures.do?clubId=%1&league=All&year=%2&allseries=true")
			 .arg(clubId)
			 .arg(currentYear));

	QNetworkRequest request(url);
	request.setRawHeader("User-Agent",
			     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
	request.setTransferTimeout(30000);
	networkManager->get(request);
}

void CricNodeFixtureBrowser::OnNetworkReply(QNetworkReply *reply)
{
	progressBar->setVisible(false);

	if (reply->error() != QNetworkReply::NoError) {
		SetStatus("Error: " + reply->errorString());
		reply->deleteLater();
		return;
	}

	QByteArray data = reply->readAll();
	QUrl requestUrl = reply->request().url();
	reply->deleteLater();

	if (requestUrl.host().contains("dallascricket")) {
		ParseDclResponse(data);
		/* Fetch more pages */
		dclPagesLoaded++;
		if (dclPagesLoaded < dclTotalPages) {
			QUrl url("https://dallascricket.org:3000/api/getmatchlists");
			QUrlQuery query;
			query.addQueryItem("offset", QString::number(dclPagesLoaded * 10));
			query.addQueryItem("query", "");
			query.addQueryItem("filter", "1");
			query.addQueryItem("isSearchById", "false");
			url.setQuery(query);

			QNetworkRequest request(url);
			request.setRawHeader("User-Agent",
					     "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
			request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
			request.setTransferTimeout(30000);
			networkManager->get(request);

			SetStatus(QString("Fetching DCL fixtures... (page %1/%2)")
					  .arg(dclPagesLoaded + 1)
					  .arg(dclTotalPages));
			progressBar->setVisible(true);
		} else {
			PopulateTable();
		}
	} else if (requestUrl.host().contains("play-cricket")) {
		ParsePlayCricketResponse(data);
		PopulateTable();
	} else if (requestUrl.host().contains("playhq")) {
		ParsePlayHQResponse(data);
		PopulateTable();
	} else if (requestUrl.host().contains("cricclubs")) {
		ParseCricClubsResponse(data);
		PopulateTable();
	}
}

void CricNodeFixtureBrowser::ParseDclResponse(const QByteArray &data)
{
	QJsonDocument doc = QJsonDocument::fromJson(data);
	QJsonObject root = doc.object();
	if (!root["success"].toBool())
		return;

	QJsonArray matchList = root["matchLists"].toArray();
	for (auto val : matchList) {
		QJsonObject m = val.toObject();
		CricNodeFixture f;
		f.provider = "dcl";
		f.matchId = QString::number(m["id"].toInt()).toStdString();
		f.team1 = m["team1Name"].toString().toStdString();
		f.team2 = m["team2Name"].toString().toStdString();
		f.date = m["date"].toString().toStdString();
		f.time = m["start_time"].toString().toStdString();
		f.venue = m["ground_name"].toString().toStdString();

		if (m["is_live"].toInt() == 1)
			f.status = "LIVE";
		else if (m["is_match_ended"].toBool())
			f.status = "COMPLETED";
		else
			f.status = "SCHEDULED";

		f.hasScorecard = (f.status != "SCHEDULED");
		fixtures.push_back(f);
	}
}

void CricNodeFixtureBrowser::ParsePlayCricketResponse(const QByteArray &data)
{
	QJsonDocument doc = QJsonDocument::fromJson(data);
	QJsonObject root = doc.object();
	QJsonArray results = root["result_summary"].toArray();

	for (auto val : results) {
		QJsonObject m = val.toObject();
		CricNodeFixture f;
		f.provider = "playcricket";
		f.matchId = QString::number(m["id"].toInt()).toStdString();
		f.team1 = m["home_team_name"].toString().toStdString();
		f.team2 = m["away_team_name"].toString().toStdString();
		f.date = m["match_date"].toString().toStdString();
		f.time = m["match_time"].toString().toStdString();
		f.venue = m["ground_name"].toString().toStdString();

		QString status = m["status"].toString();
		if (status == "InProgress")
			f.status = "LIVE";
		else if (status == "Completed")
			f.status = "COMPLETED";
		else if (status == "Cancelled")
			f.status = "CANCELLED";
		else
			f.status = "SCHEDULED";

		f.hasScorecard = (f.status == "LIVE" || f.status == "COMPLETED");
		fixtures.push_back(f);
	}
}

void CricNodeFixtureBrowser::ParsePlayHQResponse(const QByteArray &data)
{
	QJsonDocument doc = QJsonDocument::fromJson(data);
	QJsonObject root = doc.object();
	QJsonArray rounds = root["rounds"].toArray();

	/* Build team name lookup */
	QJsonArray teamsArr = root["teams"].toArray();
	QMap<QString, QString> teamNames;
	for (auto t : teamsArr) {
		QJsonObject team = t.toObject();
		teamNames[team["id"].toString()] = team["name"].toString();
	}

	for (auto rv : rounds) {
		QJsonObject round = rv.toObject();
		QJsonArray games = round["games"].toArray();

		for (auto gv : games) {
			QJsonObject game = gv.toObject();
			CricNodeFixture f;
			f.provider = "playhq";
			f.matchId = game["id"].toString().toStdString();

			QJsonArray gameTeams = game["teams"].toArray();
			if (gameTeams.size() >= 2) {
				f.team1 = gameTeams[0].toObject()["name"]
						  .toString()
						  .toStdString();
				f.team2 = gameTeams[1].toObject()["name"]
						  .toString()
						  .toStdString();
			}

			QJsonArray schedule = game["schedule"].toArray();
			if (!schedule.isEmpty()) {
				QString dt = schedule[0].toObject()["dateTime"].toString();
				if (dt.contains("T")) {
					f.date = dt.left(10).toStdString();
					f.time = dt.mid(11, 5).toStdString();
				}
			}

			QString status = game["status"].toString();
			if (status == "LIVE")
				f.status = "LIVE";
			else if (status == "COMPLETED")
				f.status = "COMPLETED";
			else
				f.status = "SCHEDULED";

			f.hasScorecard = (f.status != "SCHEDULED");
			fixtures.push_back(f);
		}
	}
}

void CricNodeFixtureBrowser::ParseCricClubsResponse(const QByteArray &data)
{
	/*
	 * HTML scraping replicating CricClubsParser.kt from the Android app.
	 * Parses div.schedule-all blocks from the CricClubs fixtures page.
	 *
	 * Each block contains:
	 *   .sch-time → h2 (day number), h5[0] (month+year), h5[1] (time)
	 *   .schedule-text h3 a[href*="viewTeam"] → team names
	 *   a[href*="viewScorecard"] → scorecard link with matchId
	 *   a[href*="viewGround"] → venue name
	 *   [id^="deleteRow"] → fixtureId fallback
	 */
	QString html = QString::fromUtf8(data);

	/* Split on div.schedule-all blocks */
	QRegularExpression blockRe(
		R"(<div[^>]*class\s*=\s*"[^"]*schedule-all[^"]*"[^>]*>(.*?)</div>\s*</div>\s*</div>)",
		QRegularExpression::DotMatchesEverythingOption);

	/* More robust: split on schedule-all divs by finding start markers */
	QStringList blocks;
	int searchFrom = 0;
	QString marker = "schedule-all";
	while (true) {
		int pos = html.indexOf(marker, searchFrom);
		if (pos < 0)
			break;
		/* Find the enclosing div start */
		int divStart = html.lastIndexOf("<div", pos);
		if (divStart < 0) {
			searchFrom = pos + marker.length();
			continue;
		}
		/* Find the next schedule-all or end of content */
		int nextPos = html.indexOf(marker, pos + marker.length());
		int blockEnd = (nextPos > 0) ? html.lastIndexOf("<div", nextPos)
					     : html.length();
		if (blockEnd <= divStart)
			blockEnd = html.length();

		blocks.append(html.mid(divStart, blockEnd - divStart));
		searchFrom = (nextPos > 0) ? nextPos : html.length();
	}

	/* Helper regexes */
	QRegularExpression teamRe(
		R"(viewTeam[^"]*"[^>]*>([^<]+)<)",
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpression scorecardRe(
		R"(viewScorecard[^"]*matchId=(\d+)[^"]*clubId=(\d+))",
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpression matchIdRe(
		R"(matchId=(\d+))",
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpression venueRe(
		R"(viewGround[^"]*"[^>]*>([^<]+)<)",
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpression deleteRowRe(
		R"(deleteRow(\d+))",
		QRegularExpression::CaseInsensitiveOption);
	QRegularExpression fixtureIdRe(
		R"(fixtureId=(\d+))",
		QRegularExpression::CaseInsensitiveOption);
	/* Date from sch-time: <h2>day</h2> and <h5>MonthYear</h5> <h5>time</h5> */
	QRegularExpression dayRe(R"(<h2[^>]*>\s*(\d+)\s*</h2>)",
				 QRegularExpression::CaseInsensitiveOption);
	QRegularExpression h5Re(R"(<h5[^>]*>([^<]+)</h5>)",
				QRegularExpression::CaseInsensitiveOption);

	for (const QString &block : blocks) {
		CricNodeFixture f;
		f.provider = "cricclubs";
		f.clubId = cricClubsClubId;

		/* Extract teams */
		auto teamIt = teamRe.globalMatch(block);
		if (teamIt.hasNext())
			f.team1 = teamIt.next().captured(1).trimmed().toStdString();
		if (teamIt.hasNext())
			f.team2 = teamIt.next().captured(1).trimmed().toStdString();

		if (f.team1.empty() && f.team2.empty())
			continue; /* Skip blocks without team info */

		/* Extract matchId from scorecard link */
		auto scMatch = scorecardRe.match(block);
		if (scMatch.hasMatch()) {
			f.matchId = scMatch.captured(1).toStdString();
			f.hasScorecard = true;
		} else {
			/* Try any matchId link */
			auto midMatch = matchIdRe.match(block);
			if (midMatch.hasMatch())
				f.matchId = midMatch.captured(1).toStdString();
		}
		/* Fallback: deleteRow ID or fixtureId */
		if (f.matchId.empty()) {
			auto drMatch = deleteRowRe.match(block);
			if (drMatch.hasMatch())
				f.matchId = drMatch.captured(1).toStdString();
		}
		if (f.matchId.empty()) {
			auto fiMatch = fixtureIdRe.match(block);
			if (fiMatch.hasMatch())
				f.matchId = fiMatch.captured(1).toStdString();
		}

		/* Extract venue */
		auto venueMatch = venueRe.match(block);
		if (venueMatch.hasMatch())
			f.venue = venueMatch.captured(1).trimmed().toStdString();

		/* Extract date and time from sch-time section */
		auto dayMatch = dayRe.match(block);
		QList<QString> h5Values;
		auto h5It = h5Re.globalMatch(block);
		while (h5It.hasNext())
			h5Values.append(h5It.next().captured(1).trimmed());

		QString monthYear = h5Values.size() > 0 ? h5Values[0] : "";
		QString timeStr = h5Values.size() > 1 ? h5Values[1] : "";
		QString dayStr = dayMatch.hasMatch() ? dayMatch.captured(1) : "";

		f.date = (monthYear + " " + dayStr).trimmed().toStdString();
		f.time = timeStr.toStdString();

		/* Determine status */
		if (f.hasScorecard) {
			QString blockLower = block.toLower();
			if (blockLower.contains("won by") ||
			    blockLower.contains("tied") ||
			    blockLower.contains("draw") ||
			    blockLower.contains("no result")) {
				f.status = "COMPLETED";
			} else {
				f.status = "LIVE";
			}
		} else {
			f.status = "SCHEDULED";
		}

		fixtures.push_back(f);
	}

	if (fixtures.empty() && !html.contains("schedule-all")) {
		SetStatus("No fixtures found. The Club ID may be incorrect, "
			  "or CricClubs may have blocked the request.");
	}
}

void CricNodeFixtureBrowser::PopulateTable()
{
	fixtureTable->setRowCount((int)fixtures.size());
	for (int i = 0; i < (int)fixtures.size(); i++) {
		auto &f = fixtures[i];
		fixtureTable->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(f.team1)));
		fixtureTable->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(f.team2)));
		fixtureTable->setItem(i, 2, new QTableWidgetItem(QString::fromStdString(f.date)));
		fixtureTable->setItem(i, 3, new QTableWidgetItem(QString::fromStdString(f.time)));
		fixtureTable->setItem(i, 4, new QTableWidgetItem(QString::fromStdString(f.venue)));

		auto *statusItem = new QTableWidgetItem(QString::fromStdString(f.status));
		if (f.status == "LIVE")
			statusItem->setForeground(Qt::green);
		else if (f.status == "COMPLETED")
			statusItem->setForeground(Qt::gray);
		fixtureTable->setItem(i, 5, statusItem);
	}

	fixtureTable->resizeColumnsToContents();
	SetStatus(QString("Found %1 fixtures").arg(fixtures.size()));
}

void CricNodeFixtureBrowser::OnSelectClicked()
{
	int row = fixtureTable->currentRow();
	if (row >= 0 && row < (int)fixtures.size()) {
		selectedFixture = fixtures[row];
		accept();
	}
}

void CricNodeFixtureBrowser::SetStatus(const QString &text)
{
	statusLabel->setText(text);
}
