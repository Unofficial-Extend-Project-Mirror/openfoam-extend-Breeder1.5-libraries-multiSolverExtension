/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright held by original author
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

\*---------------------------------------------------------------------------*/


void Foam::multiSolver::setNextSolverDomain(const word& solverDomainName)
{
    if (!solverDomains_.found(solverDomainName))
    {
        FatalErrorIn("multiSolver::setNextSolverDomain")
            << "Next solverDomainName '" << solverDomainName << "' does"
            << " not exist in multiSolver dictionary.  Found entries are: "
            << solverDomains_.toc()
            << abort(FatalError);
    }

    readIfModified();

    // Check if superLoop was just incremented to prevent saving the initial
    // solverDomain data to the *next* superLoop
    label saveToSuperLoop(superLoop_);
    if (noSaveSinceSuperLoopIncrement_)
    {
        saveToSuperLoop--;
    }

    // Create archive path
    fileName archivePath
    (
        multiDictRegistry_.path()/"multiSolver"/currentSolverDomain_/name
        (
            saveToSuperLoop
        )
    );

    // Move all case/[time] to case/multiSolver/prefix/superloop/time
    archiveTimeDirs
    (
        multiDictRegistry_.path(),
        archivePath,
        purgeWriteSuperLoops_
    );

    // Create multiSolverTime dictionary
    IOdictionary multiSolverTime
    (
        IOobject
        (
            "multiSolverTime",
            multiDictRegistry_.constant(),
            multiDictRegistry_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE,
            false
        )
    );
    multiSolverTime.set("globalOffset", globalTimeOffset_);
    
    // Write multiSolverTime to the case/constant directory, then move to
    // archivePath
    multiSolverTime.regIOobject::write();
    mv(multiDictRegistry_.constantPath()/"multiSolverTime", archivePath);
    
    // tcSource is where the latest data has been moved to
    timeCluster tcSource
    (
        findLatestLocalTime
        (
            readSuperLoopTimes(currentSolverDomain_, saveToSuperLoop)
        )
    );

    // Copy previous solverDomain data for use later (needed for storeFields)
    wordList previousStoreFields(storeFields_);
    word previousSolverDomain = currentSolverDomain_;

    // Change all solverDomain data to the new solverDomain
    currentSolverDomain_ = solverDomainName;
    setSolverDomainControls(currentSolverDomain_);

    fileName sourcePath(findInstancePath(tcSource, 0));
    scalar globalTime(tcSource.globalValue(0));
    scalar localStartTime(0);

    switch (startFrom_)
    {
        case mtsFirstTime:
            localStartTime = 0;
            break;
        case mtsStartTime:
            localStartTime = startTime_;
            break;
        case mtsLatestTimeThisDomain:
            {
                timeCluster tcTemp
                (
                    findLatestLocalTime
                    (
                        readSolverDomainTimes(currentSolverDomain_)
                    )
                );
                localStartTime = tcTemp.localValue(0);
            }
            break;
        case mtsLatestTimeAllDomains:
            localStartTime = globalTime;
            break;
    }
    
    startTime_ = localStartTime;
    globalTimeOffset_ = globalTime - startTime_;

    // Give multiDictRegistry a time value (required for regIOobject::write()
    // to case/[timeValue]
    multiDictRegistry_.setTime(startTime_, 0);

    word stopAtSetting("endTime");

    if (!finished_)
    {
        // Copy the source data to case/[localTime]
        cp(sourcePath, multiDictRegistry_.path());
        mv
        (
            multiDictRegistry_.path()/sourcePath.name(),
            multiDictRegistry_.path()/Time::timeName(startTime_)
        );

        // Copy the previous domain's storeFields from its first timestep to
        // current time directory
        if (previousStoreFields.size())
        {
            fileName storedSourcePath
            (
                findInstancePath
                (
                    findClosestLocalTime
                    (
                        0,
                        readSuperLoopTimes
                        (
                            previousSolverDomain,
                            saveToSuperLoop
                        )
                    ),
                    0
                )
            );
            
            forAll (previousStoreFields, i)
            {
                // Copy the stored fields to case/[localTime].
                if (exists(storedSourcePath/previousStoreFields[i]))
                {
                    fileName storedSource(storedSourcePath/previousStoreFields[i]);
                    
                    cp
                    (
                        storedSource,
                        multiDictRegistry_.path()/Time::timeName(startTime_)
                            /previousStoreFields[i]
                    );
                }
                else
                {
                    FatalErrorIn("multiSolver::setNextSolverDomain")
                        << "Attempting to copy stored field "
                        << previousStoreFields[i] << " from "
                        << previousSolverDomain << " to "
                        << currentSolverDomain_ << " in superLoop "
                        << saveToSuperLoop << ".  File not found.  This may occur "
                        << "if " << previousSolverDomain << " is the first "
                        << "solverDomain to be initialized, and you did not put "
                        << "the stored fields into its 0/0 directory."
                        << abort(FatalError);
                }
            }
        }        
        
        swapBoundaryConditions
        (
            multiDictRegistry_.path()/Time::timeName(startTime_),
            currentSolverDomain_
        );

        // Determine localEndTime and stopAtSetting
        stopAtSetting = setLocalEndTime();
    }
    else //finished_
    {
        stopAtSetting = "noWriteNow";
    }

    // Build the new controlDict
    IOdictionary newControlDict
    (
        IOobject
        (
            Time::controlDictName,
            multiDictRegistry_.system(),
            multiDictRegistry_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE,
            false
        ),
        currentSolverDomainDict_
    );
    
    // Remove multiSolver-specific values from dictionary
    newControlDict.remove("startFrom");
    newControlDict.remove("startTime");
    newControlDict.remove("stopAt");
    newControlDict.remove("endTime");
    newControlDict.remove("iterations");
    newControlDict.remove("purgeWriteSuperLoops");
    newControlDict.remove("timeFormat");
    newControlDict.remove("timePrecision");
    newControlDict.remove("storeFields");
    newControlDict.remove("elapsedTime");
    
    // Add values to obtain the desired behaviour
    newControlDict.set("startFrom", "startTime");
    newControlDict.set("startTime", startTime_);
    newControlDict.set("stopAt", stopAtSetting);
    newControlDict.set("endTime", endTime_);
    if (multiSolverControl_.found("timeFormat"))
    {
        newControlDict.set
        (
            "timeFormat",
            word(multiSolverControl_.lookup("timeFormat"))
        );
    }
    if (multiSolverControl_.found("timePrecision"))
    {
        newControlDict.set
        (
            "timePrecision",
            readScalar(multiSolverControl_.lookup("timePrecision"))
        );
    }

    // Write the dictionary to the case directory
    newControlDict.regIOobject::write();

    // Change all the dictionaries
    swapDictionaries(currentSolverDomain_);

    // Remove noSaves flag
    if (noSaveSinceSuperLoopIncrement_)
    {
        noSaveSinceSuperLoopIncrement_ = false;
    }
}

