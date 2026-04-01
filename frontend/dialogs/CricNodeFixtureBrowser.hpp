#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>

#include <string>
#include <vector>
#include <functional>

struct CricNodeFixture {
	std::string matchId;
	std::string clubId;
	std::string team1;
	std::string team2;
	std::string date;
	std::string time;
	std::string venue;
	std::string status; /* SCHEDULED, LIVE, COMPLETED */
	std::string provider;
	bool hasScorecard = false;
};

class CricNodeFixtureBrowser : public QDialog {
	Q_OBJECT

public:
	explicit CricNodeFixtureBrowser(QWidget *parent = nullptr);
	~CricNodeFixtureBrowser();

	/* Returns selected fixture or empty if cancelled */
	CricNodeFixture GetSelectedFixture() const { return selectedFixture; }

private slots:
	void OnProviderChanged(int index);
	void OnFetchClicked();
	void OnSelectClicked();
	void OnNetworkReply(QNetworkReply *reply);

private:
	void FetchDclFixtures();
	void FetchPlayCricketFixtures();
	void FetchPlayHQFixtures();
	void FetchCricClubsFixtures();

	void ParseDclResponse(const QByteArray &data);
	void ParsePlayCricketResponse(const QByteArray &data);
	void ParsePlayHQResponse(const QByteArray &data);
	void ParseCricClubsResponse(const QByteArray &data);

	std::string cricClubsClubId;

	void PopulateTable();
	void SetStatus(const QString &text);

	QComboBox *providerCombo;
	QLineEdit *idInput;
	QLabel *idLabel;
	QLineEdit *tokenInput;
	QLabel *tokenLabel;
	QPushButton *fetchButton;
	QTableWidget *fixtureTable;
	QPushButton *selectButton;
	QPushButton *cancelButton;
	QLabel *statusLabel;
	QProgressBar *progressBar;

	QNetworkAccessManager *networkManager;
	std::vector<CricNodeFixture> fixtures;
	CricNodeFixture selectedFixture;
	std::string currentProvider;
	int dclPagesLoaded = 0;
	int dclTotalPages = 10;
};
