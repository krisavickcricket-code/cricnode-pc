#pragma once

#include <QObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <string>
#include <vector>

/*
 * Monitors scheduled CricClubs/DCL fixtures and automatically switches
 * the scorecard overlay to live when the match starts.
 *
 * Polls every 60 seconds. When a scheduled match transitions to LIVE
 * (detected by hasScorecard becoming true or status changing), updates
 * the browser source URL with the real match ID.
 */

struct MonitoredFixture {
	std::string overlayId;    /* CricNode overlay ID to update */
	std::string provider;     /* cricclubs, dcl, playcricket, playhq */
	std::string matchId;      /* Current match ID (may be fixture ID) */
	std::string clubId;       /* CricClubs club ID */
	std::string team1;
	std::string team2;
	std::string matchDate;
	bool isLive = false;      /* Already detected as live */
};

class CricNodeFixtureMonitor : public QObject {
	Q_OBJECT

public:
	static CricNodeFixtureMonitor *Instance();

	void StartMonitoring(const MonitoredFixture &fixture);
	void StopMonitoring(const std::string &overlayId);
	void StopAll();

	bool IsMonitoring(const std::string &overlayId) const;

signals:
	/* Emitted when a fixture goes live. The overlay manager should
	 * update the browser source URL with the new matchId. */
	void FixtureWentLive(QString overlayId, QString newMatchId, QString provider);

private slots:
	void Poll();
	void OnDclReply(QNetworkReply *reply);

private:
	CricNodeFixtureMonitor();

	void PollDcl(MonitoredFixture &fixture);
	void PollCricClubs(MonitoredFixture &fixture);

	void CheckDclResult(const QByteArray &data, MonitoredFixture &fixture);

	QTimer *pollTimer;
	QNetworkAccessManager *networkManager;
	std::vector<MonitoredFixture> monitoredFixtures;

	static CricNodeFixtureMonitor *instance;
};
