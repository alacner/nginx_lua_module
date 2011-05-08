<?lua
print("<br>=================<br>")
?>
<html>
<head>
<title>Login</title>
</head>

<body>
    <?lua
--local a=4; print(a)
?>
<br/>
    <?= math.pi ?>
    <? if logged then ?>
    <p>User <?= username ?> logged in</p>
    <a href="<?= logoutURL ?>">Logout</a>
    <? else ?>
    <p style="color:#ff0000"><?= ngx.get.id ?> </p>
    <form method="post" action="" enctype="multipart/form-data" >
        User name: <input name="username" maxlength="20" size="20" value="<?= ngx.get.username ?>" ><br />
        Password: <input name="pass" type="password" maxlength="20" size="20"><br />
        file1: <input name="file1" type="file"><br />
        file2: <input name="file2" type="file"><br />
        <textarea name="content" ></textarea><br />
        <input type="submit" value="Login" />
        <input type="reset" value="Reset" />
    </form>
    <? end ?>
</body>
</html>
