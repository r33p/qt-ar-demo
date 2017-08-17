#include "markermodel.h"

QMatrix4x4 qPair2Matrix(const qPair &qp){

    QMatrix4x4 ret(qp.first.toRotationMatrix());
    ret(0,3) = qp.second.x();   ret(1,3) = qp.second.y(); ret(2,3) = qp.second.z();
    return ret;

}

qPair matrix2qPair(const QMatrix4x4 &m){

    float data[]{ m(0,0),m(0,1),m(0,2),
                  m(1,0),m(1,1),m(1,2),
                  m(2,0),m(2,1),m(2,2)
                };

    QMatrix3x3 rM(data);

    return qPair{QQuaternion::fromRotationMatrix(rM), QQuaternion(0, m(0,3), m(1,3), m(2,3))};
}

QVector4D compareqPair(const qPair &qp1, const qPair &qp2){

    const double toRad = M_PI/180;

    // We first split each transformation in a translation vector vTra,
    // a rotation axis vRotA and an angle fAngl.
    QVector3D vTra1 = QVector3D(qp1.second.x(), qp1.second.y(), qp1.second.z()),
              vTra2 = QVector3D(qp2.second.x(), qp2.second.y(), qp2.second.z());

    QVector3D vRotA1, vRotA2; float fAngl1, fAngl2;

    qp1.first.getAxisAndAngle(&vRotA1, &fAngl1);
    qp2.first.getAxisAndAngle(&vRotA2, &fAngl2);

    // Make sure the two rotation axes are comparable.
    if(vRotA1.z() < 0 ){ vRotA1 *= -1; fAngl1 = 360 - fAngl1; }
    if(vRotA2.z() < 0 ){ vRotA2 *= -1; fAngl2 = 360 - fAngl2; }

    // Calculate ratio between the length of the two translation vectors and map them to the interval [0,2].
    double ratioLenT;
    double lenT1 = vTra1.length(), lenT2 = vTra2.length();
    if(lenT1 == 0 && lenT2 == 0)
        ratioLenT = 0;
    else if(lenT1 == 0 || lenT2 == 0)
        ratioLenT = 2;
    else
        ratioLenT = lenT2 > lenT1 ? (1-(lenT1/lenT2))*2. : (1-(lenT2/lenT1))*2.;

    // Calculate the distance between the two normalized translation vectors.
    vTra1.normalize(); vTra2.normalize();
    double distanceNormalT = fabs((vTra1 - vTra2).length());

    // Calculate the distance between a point perpendicular to the rotation axis
    // with length one and this point rotated by the angle difference.
    double distanceNormalPointR = sqrt(2*(1-cos((fAngl1-fAngl2)*toRad)));

    // Calculate the distance between the two rotation vectors.
    vRotA1.normalize(); vRotA2.normalize();
    double distanceNormalR = fabs((vRotA1 - vRotA2).length());

    // Return a vector encoding the "difference" between two transformations.
    // For equal transformation the method returns QVector4D(0,0,0,0).
                        //x             //y                 //z                     //w
    return QVector4D(   ratioLenT,     distanceNormalT,    distanceNormalPointR,   distanceNormalR);
}

QQuaternion avgAndNormalizeQuaternions(const QQuaternion &q1, const QQuaternion &q2){

    QQuaternion ret = QQuaternion( (q1.scalar() + q2.scalar()) / 2.,
                                   (q1.x() + q2.x()) / 2.,
                                   (q1.y() + q2.y()) / 2.,
                                   (q1.z() + q2.z()) / 2. );

    return ret.normalized();
}

QQuaternion avgQuaternions(const QQuaternion &q1, const QQuaternion &q2){

    QQuaternion ret = QQuaternion( (q1.scalar() + q2.scalar()) / 2.,
                                   (q1.x() + q2.x()) / 2.,
                                   (q1.y() + q2.y()) / 2.,
                                   (q1.z() + q2.z()) / 2.
                                 );
    return ret;
}

bool equalTransformation(const qPair &qp1, const qPair &qp2){

    // Parameter to decide if to transforamtion are equal.              ratioLenT   distanceNormalT  distanceNormalPointR  distanceNormalR
    const QVector4D thTransformationEquality = QVector4D(        0.2,        0.2,             0.2,                  0.2);

    QVector4D diff = compareqPair(qp1, qp2);

    return diff.x() < thTransformationEquality.x() && diff.y() < thTransformationEquality.y() &&
           diff.z() < thTransformationEquality.z() && diff.w() < thTransformationEquality.w();
}

// Constructor

MarkerModel::MarkerModel(){

   // Monitoring functionality
   {
    // Necessary to allow connections to marker model monitor
    qRegisterMetaType<Timestamp>(); qRegisterMetaType<qPair>(); qRegisterMetaType<std::string>();

    // Create a new monitor object
    MarkerModelMonitor* monitor = new MarkerModelMonitor;
    monitor->moveToThread(&monitorThread);

    connect(this, &MarkerModel::linkUpdate, monitor, &MarkerModelMonitor::monitorLinkUpdate);
    connect(this, &MarkerModel::transformationUpdate, monitor, &MarkerModelMonitor::monitorTransformationUpdate);

    connect(this, &MarkerModel::startMonitor, monitor, &MarkerModelMonitor::startMonitoring);
    connect(this, &MarkerModel::stopMonitor, monitor, &MarkerModelMonitor::stopMonitoring);

    connect(this, &MarkerModel::registerLinkUpdateToMonitor, monitor, &MarkerModelMonitor::registerLinkUpdateToMonitor);
    connect(this, &MarkerModel::registerTransformationToMonitor, monitor, &MarkerModelMonitor::registerTransformationToMonitor);


    monitorThread.start();
    }
}

// Main functions for the qml interface

void MarkerModel::markerPositionUpdated(){

    Timestamp tsNow = std::chrono::high_resolution_clock::now();

    QObject *sender = QObject::sender();
    Landmark *senderLandmark = qobject_cast<Landmark*>(sender);

    QMatrix4x4 pose = senderLandmark->pose();
    double confidence = senderLandmark->confidence();
    std::string markerID = (senderLandmark->identifier).toStdString();

    if(confidence >= thConfidenceMarkerUpdate)
        registerLink(markerID, camID, tsNow, pose, confidence);
    else
        try { updateLinkConfidence(markerID, camID, confidence); }
        catch(NoSuchLinkFoundException e) { /* Quality can just be updated if there was already a successful update. */ }

    emit linkUpdate(camID, markerID, tsNow, matrix2qPair(pose), confidence);
}

void MarkerModel::updateModel(){

    // Helper lambda's

    /* REMARK:
     * It would also be possible to implement these functions as part of the StampedTransformationWithConfidence
     * object or as proteced function of the MarkerModel class. But since the implementation strongly depends
     * on implentation the model it's more consistent to implement it here. */

    // SART == StampedTransformationWithConfidence
    // For more information about this type check out transmem.h.

    // Multiplication of two SART objects.
    auto SARTMultiplier = [](const StampedTransformationWithConfidence &lhs, const StampedTransformationWithConfidence &rhs){
        StampedTransformationWithConfidence ret;

        ret.time = lhs.time;

        ret.rotation = lhs.rotation * rhs.rotation;
        ret.translation = lhs.rotation * rhs.translation * lhs.rotation.conjugated();
        ret.translation = ret.translation + lhs.translation;

        ret.averageLinkConfidence = std::max(lhs.averageLinkConfidence, rhs.averageLinkConfidence);
        ret.maxDistanceToEntry = std::max(lhs.maxDistanceToEntry, rhs.maxDistanceToEntry);

        return ret;
    };

    // Averaging two SART objects if the encode an "equal" transformation.
    auto SARTAveragerIfEqual = [this](StampedTransformationWithConfidence &lhs, StampedTransformationWithConfidence &rhs){

        bool encodeEqualTransformation = equalTransformation(qPair{lhs.rotation, lhs.translation}, qPair{rhs.rotation, rhs.translation});

        if(!encodeEqualTransformation)
            return lhs;

        StampedTransformationWithConfidence ret;

        ret.rotation = avgAndNormalizeQuaternions(lhs.rotation, rhs.rotation);
        ret.translation = avgQuaternions(lhs.translation, rhs.translation);
        ret.averageLinkConfidence = (lhs.averageLinkConfidence + rhs.averageLinkConfidence ) / 2;
        ret.maxDistanceToEntry = (lhs.maxDistanceToEntry + rhs.maxDistanceToEntry) / 2.;
        ret.time = lhs.time;

        return ret;
    };

    // Invert the tranformation encoded in a SART object.
    auto SARTInverter = [](StampedTransformationWithConfidence &lhs){

        lhs.rotation = lhs.rotation.inverted();
        lhs.translation = -(lhs.rotation*lhs.translation*lhs.rotation.conjugated());

        return lhs;
    };

    // Actual implementation of the model:

    // Make sure there is a world center marker
    if(!worldCenterMarker){
        qWarning() << "No world center marker available. Returning.";
        return;
    }

    Timestamp tsNow = std::chrono::high_resolution_clock::now();

    const std::string worldID = (worldCenterMarker->identifier).toStdString();

    StampedTransformationWithConfidence world2camNow;
    try{ world2camNow = getLink(worldID, camID, tsNow); }
    catch(NoSuchLinkFoundException){
        // World center marker has to be seen once, so we return here.
        return;
    }

    worldCenterMarker->visible = !(world2camNow.averageLinkConfidence < thConfidenceMarkerVisible);
    emit worldCenterMarker->visibilityUpdated();

    unsigned int numberOfRelativeMarker = relativeMarkers.count();

    // If no additional marker are available, we can't do that much..
    if(numberOfRelativeMarker < 1){

        worldCenterMarker->relativePose = qPair2Matrix(qPair{world2camNow.rotation, world2camNow.translation});
        emit worldCenterMarker->relativePoseUpdated();

        return;
    }

    // Storage for all relative marker which can be updated
    QVector<Landmark*> updatableMarker = QVector<Landmark*>();

    // Storage for all transformations mapping from a relative marker to the camera
    QVector<StampedTransformationWithConfidence>  cam2relativeMarkersNow = QVector<StampedTransformationWithConfidence>();

    // Storage for all best transformations mapping from the world center to a relative marker
    QVector<StampedTransformationWithConfidence> world2relativeMarkersFix = QVector<StampedTransformationWithConfidence>();

    StampedTransformationWithConfidence relativeMarker2camNow, world2relativeMarkerFix, world2camOption;
    std::string markerID;
    for(Landmark* relativeMarker : relativeMarkers){

        // just make sure we dont run into a null pointer here
        if(relativeMarker == nullptr)
            continue;

         markerID = (relativeMarker->identifier).toStdString();

         try{ relativeMarker2camNow = getLink(markerID, camID, tsNow); }
         catch(NoSuchLinkFoundException){ continue; /* no link registered yet */ }

         try{ world2relativeMarkerFix = getBestLink(worldID, markerID); }
         catch(NoSuchLinkFoundException){ continue; /* no link registered yet */ }

         // if the confidence is to low we assume the marker is not visible
         relativeMarker->visible = !(relativeMarker2camNow.averageLinkConfidence < thConfidenceMarkerVisible);
         emit relativeMarker->visibilityUpdated();

        world2camOption = SARTMultiplier(relativeMarker2camNow, world2relativeMarkerFix);

        // if the existing mapping from world to the camera is already "good" and the same is true for the
        // calculated alternative we average the two mappings
        // if not, we take just the better one
        if( world2camNow.maxDistanceToEntry < thDistanceToLastUpdate &&
            world2camOption.maxDistanceToEntry < thDistanceToLastUpdate )

            world2camNow = SARTAveragerIfEqual(world2camNow, world2camOption);
        else
            world2camNow = world2camNow.maxDistanceToEntry < world2camOption.maxDistanceToEntry ? world2camNow : world2camOption;

        updatableMarker.push_back(relativeMarker);
        cam2relativeMarkersNow.push_back(SARTInverter(relativeMarker2camNow));
        world2relativeMarkersFix.push_back(world2relativeMarkerFix);

      }

    worldCenterMarker->relativePose = qPair2Matrix(qPair{world2camNow.rotation, world2camNow.translation});
    emit worldCenterMarker->relativePoseUpdated();

    // If no transformation from world to camera is good enough, that is the smallest value of maxDistanceToEntry
    // for all three transformation is larger than the threshold thDistanceToLastUpdate, we set the world center
    // marker to inactive.
    if(world2camNow.maxDistanceToEntry > thDistanceToLastUpdate){
        worldCenterMarker->visible = false;
        emit worldCenterMarker->visibilityUpdated();
    }

    StampedTransformationWithConfidence world2relativeMarkerNow;
    for(int indx = 0; indx < updatableMarker.count(); indx++){

        Landmark* updatedMarker = updatableMarker.at(indx);

        // Use the best transformation known from world to cam together with the current
        // transformation from the orange house marker to the cam to calculate this transformation
        // without having to query transmem again.
        world2relativeMarkerNow = SARTMultiplier(cam2relativeMarkersNow.at(indx), world2camNow);
        world2relativeMarkerFix = world2relativeMarkersFix.at(indx);

        if( world2relativeMarkerNow.maxDistanceToEntry < thDistanceToLastUpdate &&
            world2relativeMarkerFix.maxDistanceToEntry < thDistanceToLastUpdate )

            world2relativeMarkerNow = SARTAveragerIfEqual(world2relativeMarkerNow, world2relativeMarkerFix);
        else
            world2relativeMarkerNow = world2relativeMarkerNow.maxDistanceToEntry < world2relativeMarkerFix.maxDistanceToEntry ?
                        world2relativeMarkerNow : world2relativeMarkerFix;

        updatedMarker->relativePose = qPair2Matrix(qPair{world2relativeMarkerNow.rotation, world2relativeMarkerNow.translation});
        updatedMarker->relativePoseUpdated();

        if(world2relativeMarkerNow.maxDistanceToEntry > thDistanceToLastUpdate){
            updatedMarker->visible = false;
            updatedMarker->visibilityUpdated();
        }
    }

//    // Monitoring functionality
//    {
//    // Tell the monitor about the updated transformation.
//      emit transformationUpdate(world2camID, tsNow, world2camP, world2camBest.averageLinkConfidence, world2camBest.maxDistanceToEntry);
//      emit transformationUpdate(world2orangeHouseID, tsNow, world2orangeHouseP, world2orangeHouseBest.averageLinkConfidence, world2orangeHouseBest.maxDistanceToEntry);
//      emit transformationUpdate(world2adaHouseID, tsNow, world2adaHouseP, world2adaHouseBest.averageLinkConfidence, world2adaHouseBest.maxDistanceToEntry);
//    }

}

// Monitoring functions for the qml interface

void MarkerModel::startMonitoring(){

    /* REMARK:
     * Could make the monitoring options choosable through the gui. */
    /*
    emit registerLinkUpdateToMonitor(worldID, camID);
    emit registerLinkUpdateToMonitor(orangeHouseID, camID);
    emit registerLinkUpdateToMonitor(adaHouseID, camID);

    emit registerTransformationToMonitor(world2camID);
    emit registerTransformationToMonitor(world2orangeHouseID);
    emit registerTransformationToMonitor(world2adaHouseID);
    */
    emit startMonitor();
}

void MarkerModel::stopMonitoring(){
    emit stopMonitor();
}

// Functions for monitoring

void MarkerModelMonitor::registerLinkUpdateToMonitor(const std::string &srcFrame, const std::string &destFrame){

    std::string linkID = srcFrame+destFrame;

    // Check if the link is already monitored.
    if(monitoredLinkUpdates.find(linkID) != monitoredLinkUpdates.end())
        return;                 // Link already registred to be monitored

    // Create container needed for tracking.
    monitoredLinkUpdates.insert({linkID, std::list<LinkUpdate>()});
    monitoredLinkIdentifier.insert({linkID, {srcFrame, destFrame}});
}

void MarkerModelMonitor::monitorLinkUpdate(const std::string &srcFrame, const std::string &destFrame, const Timestamp &ts,
                                           const qPair &transf, const float &conf) {

   if(!currentlyMonitoring)
       return;

  std::string linkID = srcFrame+destFrame;

  // Check if link is monitored.
  auto iter2monitoredLinkUpdates = monitoredLinkUpdates.find(linkID);
  if(iter2monitoredLinkUpdates == monitoredLinkUpdates.end())
      return;                 // Link is not monitored.

  // Add entry raw to the corresponding container.
    std::list<LinkUpdate> &refToUpdates = ((*iter2monitoredLinkUpdates).second);
  if(refToUpdates.size() < MAX_NUMBER_OF_MONITORED_UPDATES_PER_LINK)
      refToUpdates.push_back(LinkUpdate{ts, transf, conf});
}

void MarkerModelMonitor::registerTransformationToMonitor(const std::string &transID){

    if(monitoredTransformation.find(transID) != monitoredTransformation.end())
        return;                 // Transformation is already registered to be monitored.

    monitoredTransformation.insert({transID, std::list<TransformationUpdate>()});
}

void MarkerModelMonitor::monitorTransformationUpdate(const std::string &transID, const Timestamp &ts, const QMatrix4x4 &trans,
                                                     const float &averageLinkConfidence, const float &maxDistanceToEntry){
    if(!currentlyMonitoring)
        return;

    auto iter2monitoredTransformations = monitoredTransformation.find(transID);
    if(iter2monitoredTransformations == monitoredTransformation.end())
        return;             // Transformation is not monitored.

    // Add entry to the corresponding container.
    std::list<TransformationUpdate> &refToTransf = ((*iter2monitoredTransformations).second);
    if(refToTransf.size() < MAX_NUMBER_OF_MONITORED_UPDATES_PER_TRANFORMATION)
        refToTransf.push_back(TransformationUpdate{ts, matrix2qPair(trans), averageLinkConfidence, maxDistanceToEntry});
}

void MarkerModelMonitor::startMonitoring() {

   if(currentlyMonitoring)
        return;         // Already monitoring.

    monitoringStartedAt = std::chrono::high_resolution_clock::now();

    currentlyMonitoring = true;
}

void MarkerModelMonitor::stopMonitoring() {

    if(!currentlyMonitoring)
        return;         // Cannot stop monitoring if it has never started.

    // Create folder where we store all the dumped files for each analysis.
    QDateTime currentTime = QDateTime::currentDateTime();
    QString folderPath = PATH + "Analysis_" + currentTime.toString("ddMMyy_HHmmss") + "/";

    QDir dir;
    int dirExists = dir.exists(folderPath);
    if( !dirExists )
        dir.mkdir(folderPath);

    /* REMARK:
     * At the moment all possible analyses are done and written to a file for all tracked link
     * and transformation updates as soon the monitoring is stopped. It would make sense to make the analyses
     * choosable through a gui.ation strongly depends in a later step. */
    doAndWriteLinkUpdateAnalyses(folderPath);

    doAndWriteTransformationUpdateAnalyses(folderPath);

    // Clean up all container for the next monitoring session.
    monitoredLinkIdentifier.clear();
    monitoredLinkUpdates.clear();
    monitoredTransformation.clear();

    currentlyMonitoring = false;
}

void MarkerModelMonitor::doAndWriteTransformationUpdateAnalyses(const QString &path){

    // Do a transformation update analysis for all monitored transformations and write
    // each of it to a seperate file.
    auto iter2 = monitoredTransformation.begin();
    while(iter2 != monitoredTransformation.end()){
        QString transID = QString::fromStdString((*iter2).first);

        TransformationUpdateAnalysis tua = TransformationUpdateAnalysis(monitoringStartedAt, transID);
        tua.doAnalysis((*iter2).second);
        writeSingleAnalysisToFile(tua, path + "Transformation_Update_Analysis_" + transID);
        iter2++;
    }
}

void MarkerModelMonitor::doAndWriteLinkUpdateAnalyses(const QString &path){

    // Do a link update analysis for all monitored link updates and write each of it to a seperate file.
    auto iter = monitoredLinkUpdates.begin();
    while(iter != monitoredLinkUpdates.end()){

        std::string linkID = (*iter).first;
        QString srcFrame = QString::fromStdString((monitoredLinkIdentifier.at(linkID)).first);
        QString destFrame = QString::fromStdString((monitoredLinkIdentifier.at(linkID)).second);

        LinkUpdateAnalysis lua = LinkUpdateAnalysis(monitoringStartedAt, srcFrame, destFrame);
        lua.doAnalysis(monitoredLinkUpdates.at(linkID));
        writeSingleAnalysisToFile(lua, path  + "Link_Update_Analysis_" + srcFrame + "_" + destFrame);
        iter++;
    }
}

void MarkerModelMonitor::writeSingleAnalysisToFile(Analysis &analysis, const QString &path){

    const QString lineSeperator = ",", newLine = "\n";

    QFile file(path + ".m");
    if(!file.open(QFile::WriteOnly | QFile::Text)){
        qDebug() << file.errorString();
        return;
    }

    QTextStream out(&file);

    // If a LinkUpdateFixAnalysis is written to a file, the first data line contains
    // the fix transformation.
    if(typeid(analysis) == typeid(LinkUpdateFixAnalysis)){

        LinkUpdateFixAnalysis a = dynamic_cast<LinkUpdateFixAnalysis&>(analysis);

        out << QString::number(a.fixT.first.scalar())      << lineSeperator
            << QString::number(a.fixT.first.x())           << lineSeperator
            << QString::number(a.fixT.first.y())           << lineSeperator
            << QString::number(a.fixT.first.z())           << lineSeperator
            << QString::number(a.fixT.second.scalar())     << lineSeperator
            << QString::number(a.fixT.second.x())          << lineSeperator
            << QString::number(a.fixT.second.y())          << lineSeperator
            << QString::number(a.fixT.second.z())          << newLine;
    }

   for(AnalysisSingleResult &r : analysis.results)

       out << QString::number(r.tms)                      << lineSeperator
           << QString::number(r.tf.first.scalar())        << lineSeperator
           << QString::number(r.tf.first.x())             << lineSeperator
           << QString::number(r.tf.first.y())             << lineSeperator
           << QString::number(r.tf.first.z())             << lineSeperator
           << QString::number(r.tf.second.scalar())       << lineSeperator
           << QString::number(r.tf.second.x())            << lineSeperator
           << QString::number(r.tf.second.y())            << lineSeperator
           << QString::number(r.tf.second.z())            << lineSeperator
           << QString::number(r.ratioLenT)                << lineSeperator
           << QString::number(r.distanceNormalT)          << lineSeperator
           << QString::number(r.distanceNormalPointR)     << lineSeperator
           << QString::number(r.distanceNormalR)          << lineSeperator
           << QString::number(r.conf)                     << lineSeperator
           << QString::number(r.distanceToEntry)          << newLine;

    file.close();
    if(file.error()){
        qDebug() << file.errorString();
        return;
    }
}

void LinkUpdateAnalysis::doAnalysis(std::list<LinkUpdate> &input){

    // We need at least two entries to do this analysis
    if(input.size() < 2)
        return;

    // Sort all updates from ealierst to latest.
    auto comperator = ([](const LinkUpdate &lu1, const LinkUpdate &lu2) { return lu1.time > lu2.time;});
    input.sort(comperator);

    auto iter = input.begin();
    LinkUpdate preLu = (*iter), curLu;
    iter++;
    while(iter != input.end()){
        curLu = (*iter);
        // Compare transformation of current update against the transformation of previous update and store result.
        QVector4D diff = compareqPair(preLu.transformation, curLu.transformation);
        results.push_front(
                    AnalysisSingleResult{
                        ((std::chrono::duration_cast<std::chrono::milliseconds>(curLu.time - tZero)).count()),
                        curLu.transformation,
                        diff.x(),
                        diff.y(),
                        diff.z(),
                        diff.w(),
                        curLu.confidence,
                        0.
              }
       );
       preLu = curLu;
       iter++;
    }
}

void LinkUpdateFixAnalysis::doAnalysis(std::list<LinkUpdate> &input){

    // Sort all updates from ealierst to latest.
    auto comperator = ([](const LinkUpdate &lu1, const LinkUpdate &lu2) { return lu1.time > lu2.time;});
    input.sort(comperator);

    for(LinkUpdate &l : input){
        // Compare transformation of current update against the fix transformation fixT and store result.
        QVector4D diff = compareqPair(l.transformation, fixT);
        results.push_front(
                    AnalysisSingleResult{
                        (std::chrono::duration_cast<std::chrono::milliseconds>(l.time - tZero)).count(),
                        l.transformation,
                        diff.x(),
                        diff.y(),
                        diff.z(),
                        diff.w(),
                        l.confidence,
                        0
                    }
       );
    }
}

void TransformationUpdateAnalysis::doAnalysis(std::list<TransformationUpdate> &input) {

    // We need at least two entries to do this analysis.
    if(input.size() < 2)
        return;

    // Sort all transformation updates from ealierst to latest.
    auto comperator = ([](const TransformationUpdate &lu1, const TransformationUpdate &lu2) { return lu1.time > lu2.time;});
    input.sort(comperator);

    auto iter = input.begin();
    TransformationUpdate preTu = (*iter), curTu;
    iter++;
    while(iter != input.end()){
        curTu = (*iter);
        // Compare transformation of current update against the transformation of the previous update and store result.
        QVector4D diff = compareqPair(preTu.transformation, curTu.transformation);
        results.push_front(
                    AnalysisSingleResult{
                        (std::chrono::duration_cast<std::chrono::milliseconds>(curTu.time - tZero)).count(),
                        curTu.transformation,
                        diff.x(),
                        diff.y(),
                        diff.z(),
                        diff.w(),
                        curTu.avgerageLinkConfidence,
                        curTu.maxDistanceToEntry
              }
       );
       preTu = curTu;
       iter++;
    }
}


// NEW STUFF ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

QQmlListProperty<Landmark> MarkerModel::worldCenterRelativeMarkers(){
    return QQmlListProperty<Landmark>(this, this,
             &MarkerModel::appendWorldCenterRelativeMarker,
             &MarkerModel::worldCenterRelativeMarkersCount,
             &MarkerModel::worldCenterRelativeMarker,
             &MarkerModel::clearWorldCenterRelativeMarkers);
}

void MarkerModel::appendWorldCenterRelativeMarker(Landmark* worldCenterRelativeMarker) {

    connect(worldCenterRelativeMarker, &Landmark::changed, this, &MarkerModel::markerPositionUpdated);

    relativeMarkers.append(worldCenterRelativeMarker);
}

int MarkerModel::worldCenterRelativeMarkersCount() const
{
    return relativeMarkers.count();
}

Landmark* MarkerModel::worldCenterRelativeMarker(int i) const
{
    return relativeMarkers.at(i);
}

void MarkerModel::clearWorldCenterRelativeMarkers() {
    return relativeMarkers.clear();
}

void MarkerModel::appendWorldCenterRelativeMarker(QQmlListProperty<Landmark>* list, Landmark* p) {
    reinterpret_cast< MarkerModel* >(list->data)->appendWorldCenterRelativeMarker(p);
}

void MarkerModel::clearWorldCenterRelativeMarkers(QQmlListProperty<Landmark>* list) {
    reinterpret_cast< MarkerModel* >(list->data)->clearWorldCenterRelativeMarkers();
}

Landmark* MarkerModel::worldCenterRelativeMarker(QQmlListProperty<Landmark>* list, int i) {
    return reinterpret_cast< MarkerModel* >(list->data)->worldCenterRelativeMarker(i);
}

int MarkerModel::worldCenterRelativeMarkersCount(QQmlListProperty<Landmark>* list) {
    return reinterpret_cast< MarkerModel* >(list->data)->worldCenterRelativeMarkersCount();
}
