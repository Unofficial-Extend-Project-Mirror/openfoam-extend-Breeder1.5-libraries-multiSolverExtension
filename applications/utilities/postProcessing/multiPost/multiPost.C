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

Application
    multiPost

Description
    Post-processor utility required for working with multiSolver-enabled
    applications.  These applications store data output in a different
    location than usual.  This utility loads and unloads data from that
    location to where post-processors expect it.

Author
    David L. F. Gaden

\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "multiSolver.H"

void parseOptions
(
    wordList * ptrSolverDomains,
    labelList * ptrSuperLoops,
    string options
)
{
    IStringStream optionsStream(options);
    label nSolverDomains(0);
    label nSuperLoops(0);
    
    // Get solverDomainNames, if any
    while (not optionsStream.eof())
    {
        token nextOption(optionsStream);
        if (nextOption.isLabel())
        {
            ptrSuperLoops->setSize(++nSuperLoops);
            ptrSuperLoops->operator[](nSuperLoops - 1)
                = nextOption.labelToken();
            break;
        }
        if (nextOption.isWord())
        {
            ptrSolverDomains->setSize(++nSolverDomains);
            ptrSolverDomains->operator[](nSolverDomains - 1)
                = nextOption.wordToken();
        }
        else
        {
            // not word, not label, fail
            FatalErrorIn("multiPost::parseOptions")
                << "Expecting word or label.  Neither found at position "
                << nSolverDomains - 1 << " in " << options
                << abort(FatalError);
        }
    }
    
    // Get superLoopList
    while (not optionsStream.eof())
    {
        token nextOption(optionsStream);
        if (nextOption.isLabel())
        {
            ptrSuperLoops->setSize(++nSuperLoops);
            ptrSuperLoops->operator[](nSuperLoops - 1)
                = nextOption.labelToken();
        }
        else if (nSuperLoops > 0) 
        {
            // might be a range -> label : label

            if (nextOption.isPunctuation())
            {
                token::punctuationToken p(nextOption.pToken());
                if (p == token::COLON)
                {
                    token nextNextOption(optionsStream);
                    if (nextNextOption.isLabel())
                    {
                        label toValue(nextNextOption.labelToken());
                        label fromValue
                        (
                            ptrSuperLoops->operator[](nSuperLoops - 1)
                        );
                        
                        if (toValue > fromValue)
                        {
                            // correct range format
                            for (label i = fromValue + 1; i <= toValue; i++)
                            {
                                ptrSuperLoops->setSize(++nSuperLoops);
                                ptrSuperLoops->operator[](nSuperLoops - 1) = i;
                            }
                        }
                        else
                        {
                            // greater than / less than, range
                            FatalErrorIn("multiPost::parseOptions")
                                << "superLoop range incorrect order.  'from : "
                                << "to' where 'from' should be less than "
                                << "'to'.  Values read are '" << fromValue
                                << " : " << toValue
                                << abort(FatalError);
                        }
                    }
                    else
                    {
                        // nextNext not label
                        FatalErrorIn("multiPost::parseOptions")
                            << "Incorrect syntax.  Expecting label after ':' "
                            << "in " << options
                            << abort(FatalError);
                    }
                }
                else
                {
                    // non : punctuation
                    FatalErrorIn("multiPost::parseOptions")
                        << "Incorrect syntax.  Expecting label, word, or ':' "
                        << "in " << options
                        << abort(FatalError);
                }
            }
            else
            {
                // not punctuation
                FatalErrorIn("multiPost::parseOptions")
                    << "Incorrect syntax.  Expecting label, word, or ':' "
                    << "in " << options
                    << abort(FatalError);
            }
        }
        else
        {
            // not label, not word
            FatalErrorIn("multiPost::parseOptions")
                << "Incorrect syntax.  Expecting label, word, or ':' "
                << "in " << options
                << abort(FatalError);
        }
    }
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    argList::validOptions.insert("list","");
    argList::validOptions.insert
    (
        "load","<[solverDomainName] [superLoopNumber(s)]>"
    );
    argList::validOptions.insert
    (
        "purge","<[solverDomainName] [superLoopNumber(s)]>"
    );
    argList::validOptions.insert("set","<solverDomainName>");
    argList::validOptions.insert("global","");
    argList::validOptions.insert("local","");
    // default behaviour is purge the case/[time] directory before '-load'
    // command.  '-noPurge' prevents this.  Allows for more complicated
    // load data selections by executing multiPost several times
    argList::validOptions.insert("noPurge","");
    // default behaviour is: if there is only one solverDomain specified, use
    // setSolverDomain() on it.  Same as multiPost -set solverDomain.
    // '-noSet' prevents this.
    argList::validOptions.insert("noSet","");
    // default behaviour is: if there are storeFields defined, when loading, it
    // will copy the store fields into every time instance where they are
    // absent.
    argList::validOptions.insert("noStore","");

#   include "setRootCase.H"

    enum commandType {list, load, purge, set};
    commandType command;
    string options;
    bool global = false;
    bool local = false;
    bool all = false;
    bool root = false;
    bool noPurge = false;
    bool noSet = false;
    bool noStore = false;
    label nCommands(0);
    
    // Read arguments
    if (args.options().found("list"))
    {
        nCommands++;
        command = list;
    }
    if (args.options().found("load"))
    {
        nCommands++;
        command = load;
        options = args.options()["load"];
    }
    if (args.options().found("purge"))
    {
        nCommands++;
        command = purge;
        options = args.options()["purge"];
    }
    if (args.options().found("set"))
    {
        nCommands++;
        command = set;
        options = args.options()["set"];
    }
    if (args.options().found("global"))
    {
        global = true;
    }
    if (args.options().found("local"))
    {
        local = true;
    }
    if (args.options().found("noPurge"))
    {
        noPurge = true;
    }
    if (args.options().found("noSet"))
    {
        noSet = true;
    }
    if (args.options().found("noStore"))
    {
        noStore = true;
    }

    // Error checking
    if (nCommands == 0)
    {
        FatalErrorIn("multiPost::main")
            << "multiPost - nothing to do.  Use 'multiPost -help' for assistance."
            << abort(FatalError);
    }
    else if (nCommands > 1)
    {
        FatalErrorIn("multiPost::main")
            << "More than one command found.  Use only one of:\n\t-list"
            << "\n\t-list"
            << "\n\t-purge"
            << "\n\t-set\n"
            << abort(FatalError);
    }
    if (global && local)
    {
        FatalErrorIn("multiPost::main")
            << "Options global and local both specified.  Use only one or "
            << "none."
            << abort(FatalError);
    }
    if ((command != load) && (noPurge || noSet || noStore))
    {
        FatalErrorIn("multiPost::main")
            << "'noPurge', 'noSet' and 'noStore' can only be used with the "
            << "'-load' command."
            << abort(FatalError);
    }

    multiSolver multiRun
    (
        Foam::multiSolver::multiControlDictName,
        args.rootPath(),
        args.caseName()
    );
    
    const IOdictionary& mcd(multiRun.multiControlDict());
    wordList solverDomains(0);
    labelList superLoops(0);
    if (command != list)
    {
        parseOptions(&solverDomains, &superLoops, options);
    }

    // Special words - all, root
    if (solverDomains.size() == 1)
    {
        if (solverDomains[0] == "all")
        {
            all = true;
        }
        else if (solverDomains[0] == "root")
        {
            root = true;
        }
    }

    // More error checking
    if (root && ((command == load) || (command == set)))
    {
        FatalErrorIn("multiPost::main")
            << "'root' is not a valid option with '-load' or '-set'"
            << abort(FatalError);
    }
    if (all && (command == set))
    {
        FatalErrorIn("multiPost::main")
            << "'all' is not a valid option with '-set'"
            << abort(FatalError);
    }
    if ((command == set) && ((solverDomains.size() > 1) || superLoops.size()))
    {
        FatalErrorIn("multiPost::main")
            << "'-set' can only have a single solverDomain name as an option."
            << abort(FatalError);
    }
    if (all && superLoops.size())
    {
        FatalErrorIn("multiPost::main")
            << "'all' cannot be followed by superLoop numbers.  To specify "
            << "a superLoop range for all solverDomains, omit the solverDomain"
            << " name entirely.  e.g. multiPost -load 0:4 6"
            << abort(FatalError);
    }
    if (root && superLoops.size())
    {
        FatalErrorIn("multiPost::main")
            << "'root' cannot be followed by superLoop numbers.  'root' refers"
            << " to case/[time] directories.  There are no superLoops here."
            << abort(FatalError);
    }

    // Check for correct solverDomain names
    if (!all && !root && (command != list))
    {
        forAll(solverDomains, i)
        {
            if (solverDomains[i] == "default")
            {
                // default not permitted
                FatalErrorIn("multiPost::main")
                    << "'default' is not a permitted solverDomain name."
                    << abort(FatalError);
            }
            if (!mcd.subDict("solverDomains").found(solverDomains[i]))
            {
                // Incorrect solver domain name
                FatalErrorIn("multiPost::main")
                    << "solverDomainName " << solverDomains[i] << "is not "
                    << "found."
                    << abort(FatalError);
            }
        }
    }

    // Load specified timeClusterLists
    timeClusterList tclSource(0);

    if (all)
    {
        // read all
        tclSource = multiRun.readAllTimes();
        forAll(tclSource, i)
        {
            if (tclSource[i].superLoop() == -1)
            {
                tclSource[i].times().clear();
            }
        }
        tclSource.purgeEmpties();
    }
    else if ((!superLoops.size()) && (command != set) && (command != list))
    {
        // no superLoops specified - read entire solverDomains
        forAll (solverDomains, sd)
        {
            tclSource.append
            (
                multiRun.readSolverDomainTimes(solverDomains[sd])
            );
        }
    }
    else if ((!root) && (command != set) && (command != list))
    {
        // read individual superLoops
        if (!solverDomains.size())
        {
            solverDomains = mcd.subDict("solverDomains").toc();
        }
        forAll(superLoops, sl)
        {
            forAll(solverDomains, sd)
            {
                if (solverDomains[sd] == "default") continue;
                tclSource.append
                (
                    multiRun.readSuperLoopTimes
                    (
                        solverDomains[sd],
                        superLoops[sl]
                    )
                );
            }
        }
    }

    if (tclSource.size())
    {
        if (!tclSource.purgeEmpties())
        {
            FatalErrorIn("multiPost::main")
                << "No data found with specified parameters."
                << abort(FatalError);
        }
    }    

    switch (command)
    {
        case list:
        {
            Info << "Listing available data:\n" << endl;
            Info << "superLoops by solverDomain:" << endl;
            solverDomains = mcd.subDict("solverDomains").toc();
            fileName listPath
            (
                multiRun.multiDictRegistry().path()/"multiSolver"
            );
            
            forAll(solverDomains, i)
            {
                if (solverDomains[i] == "default") continue;
                Info << solverDomains[i] << ":" << endl;
                Info << multiRun.findSuperLoops(listPath/solverDomains[i])
                    << endl;
            }
            Info << endl;
            break;
        }
        case load:
        {
            // Default behaviour - use local time unless overlapping, then use
            // global time; if global overlaps, fail.  -local and -global force
            // the behaviour
            bool localOverlap(!multiRun.nonOverlapping(tclSource, false));
            bool globalOverlap(!multiRun.nonOverlapping(tclSource, true));
            if (local && localOverlap)
            {
                FatalErrorIn("multiPost::main")
                    << "'-local' option used for data with overlapping local "
                    << "values.  Try using a single solverDomain / superLoop, "
                    << "or leave '-local' off."
                    << abort(FatalError);
            }
            if (globalOverlap)
            {
                FatalErrorIn("multiPost::main")
                    << "globalTime values are overlapping.  This should not "
                    << "happen.  Ensure you have not specified the same "
                    << "solverDomain and/or superLoop more than once.  If "
                    << "that fails, try using 'multiPost -purge all' and "
                    << "rerunning the simulation.  If the problem persists, "
                    << "it is a bug."
                    << abort(FatalError);
            }

            if (!noPurge)
            {
                multiRun.purgeTimeDirs(multiRun.multiDictRegistry().path());
            }
            if
            (
                !multiRun.loadTimeClusterList
                (
                    tclSource,
                    global || localOverlap,
                    !noStore
                )
            )
            {
                FatalErrorIn("multiRun::main")
                    << "loadTimeClusterList failed.  timeClusterList contents: "
                    << tclSource
                    << abort(FatalError);
            }
            break;
        }
        case purge:
            if (root)
            {
                multiRun.purgeTimeDirs(multiRun.multiDictRegistry().path());
            }
            else
            {
                forAll(tclSource, i)
                {
                    // do not purge 'initial' directory, even if specified
                    if (tclSource[i].superLoop() < 0) continue;
                    fileName purgePath(multiRun.findInstancePath(tclSource[i], 0).path());
                    rmDir(purgePath);
                }
            }
            break;
        case set:
            // do nothing here
            break;
    }

    // Execute set command - either from an explicit '-set' or from a '-load'
    // with only one solverDomain as an option

    if 
    (
        (command == set)
     || (
            (command == load)
         && (solverDomains.size() == 1)
         && (!all)
        )
    )
    {
        multiRun.setSolverDomainPostProcessing(solverDomains[0]);
    }

    Info << "\nCommand completed successfully.\n" << endl;
    return(0);
}

// ************************************************************************* //
