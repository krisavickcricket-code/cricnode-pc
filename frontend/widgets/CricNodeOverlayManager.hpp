#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>

#include <obs.h>
#include <string>
#include <vector>

class CricNodeFixtureMonitor;

struct CricNodeOverlay {
	std::string id;
	std::string name;
	std::string type; /* "image", "image_rotation", "url", "scorecard" */
	bool enabled = true;

	/* Image overlay */
	std::string imagePath;

	/* Image rotation */
	std::vector<std::string> rotationImagePaths;
	int rotationIntervalSec = 5;

	/* URL overlay */
	std::string url;

	/* Scorecard overlay */
	std::string scorecardProvider; /* cricclubs, playcricket, playhq, dcl */
	std::string matchId;
	std::string clubId;
	std::string team1;
	std::string team2;
	std::string matchDate;

	/* Screen position (normalized 0-1) */
	float screenX = 0.0f;
	float screenY = 0.0f;
	float screenW = 1.0f;
	float screenH = 1.0f;

	QJsonObject toJson() const;
	static CricNodeOverlay fromJson(const QJsonObject &obj);
};

class CricNodeOverlayManager : public QWidget {
	Q_OBJECT

public:
	explicit CricNodeOverlayManager(QWidget *parent = nullptr);
	~CricNodeOverlayManager();

	void SaveConfig();
	void LoadConfig();

	const std::vector<CricNodeOverlay> &GetOverlays() const { return overlays; }

signals:
	void OverlaysChanged();

private slots:
	void AddOverlayClicked();
	void AddImageOverlay();
	void AddImageRotationOverlay();
	void AddUrlOverlay();
	void AddScorecardOverlay();
	void EditOverlay();
	void RemoveOverlay();
	void ToggleOverlay(int row);
	void UpdateRotatingImages();
	void OnFixtureWentLive(QString overlayId, QString newMatchId, QString provider);

private:
	void RefreshList();
	void ApplyOverlaysToScene();
	void CreateOrUpdateSource(const CricNodeOverlay &overlay);
	void RemoveSourceForOverlay(const std::string &overlayId);
	std::string GenerateId();
	std::string GetScorecardUrl(const CricNodeOverlay &overlay);

	QVBoxLayout *mainLayout;
	QListWidget *overlayList;
	QPushButton *addButton;
	QPushButton *editButton;
	QPushButton *removeButton;

	std::vector<CricNodeOverlay> overlays;

	/* Image rotation timer */
	QTimer *rotationTimer;
	std::map<std::string, int> currentRotationIndex;
};
