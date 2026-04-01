#include "CricNodeFixtureBrowser.hpp"

#include <QDate>
#include <QDesktopServices>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

CricNodeFixtureBrowser::CricNodeFixtureBrowser(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Browse Fixtures");
	setMinimumSize(750, 550);

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

	/* Date filter row */
	auto *dateRow = new QHBoxLayout();
	dateFilterCheck = new QCheckBox("Filter by date:");
	dateFilter = new QDateEdit(QDate::currentDate());
	dateFilter->setCalendarPopup(true);
	dateFilter->setDisplayFormat("MMM d, yyyy");
	dateFilter->setEnabled(false);
	connect(dateFilterCheck, &QCheckBox::toggled, this, [this](bool checked) {
		dateFilter->setEnabled(checked);
		/* Re-filter if we already have results */
		if (!allFixtures.empty()) {
			fixtures.clear();
			if (checked) {
				QDate d = dateFilter->date();
				for (auto &f : allFixtures) {
					/* Simple date string match */
					QString dateStr = QString::fromStdString(f.date).toLower();
					QString monthName = d.toString("MMM").toLower();
					bool dayMatch = dateStr.contains(QString::number(d.day()));
					bool monthMatch = dateStr.contains(monthName);
					bool yearMatch = dateStr.contains(QString::number(d.year()));
					if (dayMatch && monthMatch && yearMatch)
						fixtures.push_back(f);
				}
			} else {
				fixtures = allFixtures;
			}
			PopulateTable();
		}
	});
	connect(dateFilter, &QDateEdit::dateChanged, this, [this](const QDate &d) {
		if (dateFilterCheck->isChecked() && !allFixtures.empty()) {
			fixtures.clear();
			for (auto &f : allFixtures) {
				QString dateStr = QString::fromStdString(f.date).toLower();
				QString monthName = d.toString("MMM").toLower();
				bool dayMatch = dateStr.contains(QString::number(d.day()));
				bool monthMatch = dateStr.contains(monthName);
				bool yearMatch = dateStr.contains(QString::number(d.year()));
				if (dayMatch && monthMatch && yearMatch)
					fixtures.push_back(f);
			}
			PopulateTable();
		}
	});
	dateRow->addWidget(dateFilterCheck);
	dateRow->addWidget(dateFilter);
	dateRow->addStretch();
	layout->addLayout(dateRow);

	/* Buttons row: Fetch + Browse on Web */
	auto *fetchRow = new QHBoxLayout();
	fetchButton = new QPushButton("Fetch Fixtures");
	connect(fetchButton, &QPushButton::clicked, this, &CricNodeFixtureBrowser::OnFetchClicked);
	fetchRow->addWidget(fetchButton);

	browseWebButton = new QPushButton("Open in Browser");
	browseWebButton->setToolTip("Open the fixtures page in your web browser to find match IDs");
	connect(browseWebButton, &QPushButton::clicked, this, &CricNodeFixtureBrowser::OnBrowseWebClicked);
	fetchRow->addWidget(browseWebButton);
	browseWebButton->hide();

	fetchRow->addStretch();
	layout->addLayout(fetchRow);

	/* Progress */
	progressBar = new QProgressBar();
	progressBar->setVisible(false);
	layout->addWidget(progressBar);

	/* Status */
	statusLabel = new QLabel("");
	statusLabel->setWordWrap(true);
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

	browseWebButton->hide();

	if (provider == "cricclubs") {
		idLabel->setText("Club ID:");
		idInput->setPlaceholderText("e.g. 423");
		tokenLabel->hide();
		tokenInput->hide();
		browseWebButton->show();
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
	allFixtures.clear();
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

void CricNodeFixtureBrowser::OnBrowseWebClicked()
{
	QString clubId = idInput->text().trimmed();
	if (clubId.isEmpty()) {
		QMessageBox::warning(this, "Missing Info", "Please enter a Club ID first.");
		return;
	}

	int year = QDate::currentDate().year();
	QString url;

	if (currentProvider == "cricclubs") {
		url = QString("https://cricclubs.com/club/fixtures.do?clubId=%1&league=All&year=%2&allseries=true")
			      .arg(clubId)
			      .arg(year);
	}

	if (!url.isEmpty())
		QDesktopServices::openUrl(QUrl(url));
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
			     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
			     "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
	request.setRawHeader("Accept", "text/html,application/xhtml+xml");
	request.setRawHeader("Accept-Language", "en-US,en;q=0.9");
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
			allFixtures = fixtures;
			PopulateTable();
		}
	} else if (requestUrl.host().contains("play-cricket")) {
		ParsePlayCricketResponse(data);
		allFixtures = fixtures;
		PopulateTable();
	} else if (requestUrl.host().contains("playhq")) {
		ParsePlayHQResponse(data);
		allFixtures = fixtures;
		PopulateTable();
	} else if (requestUrl.host().contains("cricclubs")) {
		ParseCricClubsResponse(data);
		allFixtures = fixtures;
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
	 * Port of CricClubsParser.fixturesExtractionJs from the Android app.
	 * Parses div.schedule-all blocks from the CricClubs fixtures page HTML.
	 *
	 * Note: CricClubs may block automated requests (403). If scraping fails,
	 * the user can click "Open in Browser" to view fixtures manually.
	 */
	QString html = QString::fromUtf8(data);

	/* Check if we got blocked */
	if (html.contains("403") || html.contains("Access Denied") || html.length() < 500) {
		SetStatus("CricClubs blocked the automated request.\n"
			  "Click \"Open in Browser\" to view fixtures on the website,\n"
			  "then enter the Match ID manually using \"Cancel\" → manual entry.");
		return;
	}

	/* Find each schedule-all block start position */
	QRegularExpression schedStart("schedule-all",
				      QRegularExpression::CaseInsensitiveOption);

	QList<int> blockStarts;
	auto it = schedStart.globalMatch(html);
	while (it.hasNext()) {
		auto m = it.next();
		blockStarts.append(m.capturedStart());
	}

	if (blockStarts.isEmpty()) {
		SetStatus("No fixtures found on the CricClubs page.\n"
			  "Click \"Open in Browser\" to view fixtures manually.");
		return;
	}

	/* Helper regexes */
	QRegularExpression teamRe("viewTeam[^\"]*\"[^>]*>([^<]+)<",
				  QRegularExpression::CaseInsensitiveOption);
	QRegularExpression venueRe("viewGround[^\"]*\"[^>]*>([^<]+)<",
				   QRegularExpression::CaseInsensitiveOption);
	QRegularExpression scorecardRe("viewScorecard[^\"]*matchId=(\\d+)",
				       QRegularExpression::CaseInsensitiveOption);
	QRegularExpression matchIdRe("matchId=(\\d+)",
				     QRegularExpression::CaseInsensitiveOption);
	QRegularExpression clubIdRe("clubId=(\\d+)",
				    QRegularExpression::CaseInsensitiveOption);
	QRegularExpression deleteRowRe("deleteRow(\\d+)",
				       QRegularExpression::CaseInsensitiveOption);
	QRegularExpression fixtureIdRe("fixtureId=(\\d+)",
				       QRegularExpression::CaseInsensitiveOption);
	QRegularExpression h2Re("<h2[^>]*>(\\d+)</h2>",
				QRegularExpression::CaseInsensitiveOption);
	QRegularExpression h5Re("<h5[^>]*>([^<]+)</h5>",
				QRegularExpression::CaseInsensitiveOption);

	for (int i = 0; i < blockStarts.size(); i++) {
		int start = blockStarts[i];
		int end = (i + 1 < blockStarts.size()) ? blockStarts[i + 1] : html.size();
		QString block = html.mid(start, end - start);

		CricNodeFixture f;
		f.provider = "cricclubs";
		f.clubId = cricClubsClubId;

		/* Teams */
		auto teamIt = teamRe.globalMatch(block);
		if (teamIt.hasNext())
			f.team1 = teamIt.next().captured(1).trimmed().toStdString();
		if (teamIt.hasNext())
			f.team2 = teamIt.next().captured(1).trimmed().toStdString();

		if (f.team1.empty() && f.team2.empty())
			continue;

		/* Date: h2 = day number, h5[0] = "Mon Year", h5[1] = time */
		auto h2Match = h2Re.match(block);
		QList<QString> h5Values;
		auto h5It = h5Re.globalMatch(block);
		while (h5It.hasNext())
			h5Values.append(h5It.next().captured(1).trimmed());

		QString monthYear = h5Values.size() > 0 ? h5Values[0] : "";
		QString timeStr = h5Values.size() > 1 ? h5Values[1] : "";
		QString dayStr = h2Match.hasMatch() ? h2Match.captured(1) : "";
		f.date = (monthYear + " " + dayStr).trimmed().toStdString();
		f.time = timeStr.toStdString();

		/* Venue */
		auto venueMatch = venueRe.match(block);
		if (venueMatch.hasMatch())
			f.venue = venueMatch.captured(1).trimmed().toStdString();

		/* Scorecard link → match is scored */
		auto scMatch = scorecardRe.match(block);
		f.hasScorecard = scMatch.hasMatch();

		/* Match ID from scorecard, matchId link, deleteRow, or fixtureId */
		if (scMatch.hasMatch()) {
			f.matchId = scMatch.captured(1).toStdString();
		} else {
			auto midMatch = matchIdRe.match(block);
			if (midMatch.hasMatch())
				f.matchId = midMatch.captured(1).toStdString();
		}
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

		/* Club ID override */
		auto cidMatch = clubIdRe.match(block);
		if (cidMatch.hasMatch())
			f.clubId = cidMatch.captured(1).toStdString();

		/* Status */
		QString blockLower = block.toLower();
		if (f.hasScorecard) {
			if (blockLower.contains("won by") || blockLower.contains("tied") ||
			    blockLower.contains("draw") || blockLower.contains("no result")) {
				f.status = "COMPLETED";
			} else {
				f.status = "LIVE";
			}
		} else {
			f.status = "SCHEDULED";
		}

		if (!f.matchId.empty())
			fixtures.push_back(f);
	}

	if (fixtures.empty()) {
		SetStatus("Could not parse fixtures from CricClubs.\n"
			  "Click \"Open in Browser\" to find match IDs manually.");
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
